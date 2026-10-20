extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Replay.hpp"
#include "rep_fixture.hpp"
#include <filesystem>
#include <map>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace kuge::replication;

    // What a client shows at a snapshot: the entities and their health, by network id
    using Picture = std::map<NetworkId, int>;

    Picture pictureOf(kw::World& world)
    {
        Picture picture;

        for (kw::Entity e : world.view<Replicated, Health>()) {
            picture[world.get<Replicated>(e).id] = world.get<Health>(e).points;
        }
        return picture;
    }

    struct Match
    {
        RepSim                     sim;
        Recorder                   recorder;
        std::map<std::uint32_t, Picture> live;      //!< By tick of snapshot, as the live client showed it
        std::vector<kw::Entity>    ships;

        explicit Match(RecorderConfig config = {.tickRate = 60, .protocol = 5, .info = "dust", .keyframeSeconds = 1.0})
            : recorder(sim.registry, config)
        {
            recorder.attach(*sim.client);
        }

        // Ten seconds of a scripted match: ships come, hurt, and go
        void play(int seconds)
        {
            for (int tick = 1; tick <= seconds * 60; ++tick) {
                if (tick % 120 == 1 && ships.size() < 5) {
                    ships.push_back(sim.spawn(SHIP, static_cast<float>(ships.size()) * 10.0f, 0.0f, 100));
                }
                if (tick % 7 == 0 && !ships.empty()) {
                    sim.serverWorld.get<Health>(ships[static_cast<std::size_t>(tick) % ships.size()]).points -= 3;
                    sim.serverWorld.get<kuge::Transform2D>(ships[static_cast<std::size_t>(tick) % ships.size()]).position.x += 0.5f;
                }
                if (tick == 400 && ships.size() > 1) {
                    sim.server->untrack(ships[1]);
                    sim.serverWorld.remove(ships[1]);
                    ships.erase(ships.begin() + 1);
                }
                if (tick % 60 == 0) {
                    const std::uint8_t what[2] = {1, static_cast<std::uint8_t>(tick / 60)};

                    recorder.event(sim.client->lastTick(), 9, what);
                }
                sim.step();
                if (sim.client->lastTick() != 0) {
                    live[sim.client->lastTick()] = pictureOf(sim.clientWorld);
                }
            }
        }
    };

    // A client of its own, to replay into
    struct Screen
    {
        kw::World                        world;
        ReplicationRegistry              registry = makeRegistry();
        ReplicationClient                client;

        Screen() : client(world, registry, ReplicationClientConfig{}) {}
    };
}

Test(replay, the_same_world_at_each_tick)
{
    Match match;

    match.play(10);
    const auto file = match.recorder.bytes();
    Screen screen;
    Replayer replay(screen.registry, file, screen.client, {.protocol = 5});
    std::size_t compared = 0, wrong = 0;

    AssertStrEq(replay.info().c_str(), "dust", "the game's note comes back");
    for (int frame = 0; frame < 700 && !replay.finished(); ++frame) {
        replay.update(1.0 / 60.0);
        const auto tick = screen.client.lastTick();
        const auto then = match.live.find(tick);

        if (then != match.live.end()) {
            ++compared;
            wrong += pictureOf(screen.world) == then->second ? 0 : 1;
        }
    }
    Assert(compared > 200, "most snapshots could be compared: %zu", compared);
    AssertEq(wrong, 0u, "the replayed world equals the recorded one at every tick");
    Assert(replay.finished(), "and the replay reached its end");
}

Test(replay, seek_lands_on_a_keyframe)
{
    Match match;

    match.play(10);
    Screen screen;
    Replayer replay(screen.registry, match.recorder.bytes(), screen.client, {.protocol = 5});

    replay.seek(400.0);
    Assert(screen.client.lastTick() <= 400 && screen.client.lastTick() >= 398, "the snapshot at or before the tick: %u", screen.client.lastTick());
    const Picture& expected = match.live.at(screen.client.lastTick());

    Assert(pictureOf(screen.world) == expected, "the world of that tick, rebuilt from a keyframe and the changes after it");
    replay.update(1.0 / 60.0);
    replay.update(1.0 / 60.0);
    Assert(screen.client.lastTick() > 400 - 3, "then it goes on from there");
    replay.seek(30.0);
    Assert(pictureOf(screen.world) == match.live.at(screen.client.lastTick()), "going back works too");
    replay.seek(100000.0);
    AssertEq(static_cast<std::uint32_t>(replay.tick()), replay.lastTick(), "a seek past the end goes to the end");
}

Test(replay, slow_and_paused)
{
    Match match;

    match.play(6);
    Screen screen;
    Replayer replay(screen.registry, match.recorder.bytes(), screen.client, {.protocol = 5});

    replay.setSpeed(0.5);
    for (int i = 0; i < 60; ++i) {
        replay.update(1.0 / 60.0);
    }
    Assert(std::fabs(replay.tick() - (replay.firstTick() + 30.0)) < 0.5, "a second at half speed is half a second of the match: %f", replay.tick() - replay.firstTick());
    replay.pause(true);
    const double stopped = replay.tick();
    const std::uint32_t last = screen.client.lastTick();

    for (int i = 0; i < 30; ++i) {
        replay.update(1.0 / 60.0);
    }
    Assert(replay.tick() == stopped && screen.client.lastTick() == last, "paused: nothing moves");
    Assert(replay.paused(), "and it says so");
    // Smooth: while the replay plays at half speed the interpolated position changes at each frame
    replay.pause(false);
    int moved = 0;
    float before = -1.0f;

    for (int i = 0; i < 30; ++i) {
        replay.update(1.0 / 60.0);
        for (kw::Entity e : screen.world.view<Replicated, kuge::Transform2D>()) {
            const float x = screen.world.get<kuge::Transform2D>(e).position.x;

            if (before >= 0.0f && x != before) {
                ++moved;
            }
            before = x;
            break;
        }
    }
    Assert(moved > 5, "the picture does not freeze between snapshots: %d frames with a change", moved);
}

Test(replay, events_come_back)
{
    Match match;
    std::vector<std::pair<std::uint32_t, std::uint8_t>> told;

    match.play(5);
    Screen screen;
    Replayer replay(screen.registry, match.recorder.bytes(), screen.client, {.protocol = 5});

    replay.onEvent([&](std::uint32_t tick, std::uint16_t type, std::span<const std::uint8_t> bytes) {
        Assert(type == 9 && bytes.size() == 2, "the event as it was written");
        told.emplace_back(tick, bytes[1]);
    });
    for (int frame = 0; frame < 400; ++frame) {
        replay.update(1.0 / 60.0);
    }
    AssertEq(told.size(), 5u, "one event a second, 5 seconds");
    Assert(told[0].second == 1 && told[4].second == 5 && told[0].first < told[4].first, "in order, with their tick");
    replay.seek(100.0);
    const std::size_t before = told.size();

    replay.seek(250.0);
    AssertEq(told.size(), before, "a jump does not tell again the events it jumps over");
}

Test(replay, the_ring_keeps_the_last_seconds)
{
    Match match({.tickRate = 60, .protocol = 5, .keyframeSeconds = 1.0, .ringSeconds = 3.0});

    match.play(10);
    Screen screen;
    Replayer replay(screen.registry, match.recorder.bytes(), screen.client, {.protocol = 5});
    const double seconds = (replay.lastTick() - replay.firstTick()) / 60.0;

    Assert(seconds >= 3.0 && seconds <= 4.5, "the last 3 seconds, and up to a keyframe more: %f", seconds);
    Assert(replay.lastTick() >= 590, "up to the end of the match");
    replay.seek(replay.firstTick());
    Assert(pictureOf(screen.world) == match.live.at(screen.client.lastTick()), "it starts from a whole snapshot: the world is complete");
    Recorder full(match.sim.registry, {.tickRate = 60, .protocol = 5, .keyframeSeconds = 1.0});

    AssertEq(full.entries(), 0u, "a recorder with nothing saves a file that cannot be replayed");
    bool refused = false;

    try { Replayer(screen.registry, full.bytes(), screen.client, {.protocol = 5}); } catch (const ReplayError&) { refused = true; }
    Assert(refused, "an empty recording is refused");
}

Test(replay, wrong_files_are_refused)
{
    Match match;

    match.play(3);
    Screen screen;
    auto message = [&](std::vector<std::uint8_t> file, std::uint16_t protocol) {
        try { Replayer(screen.registry, std::move(file), screen.client, {.protocol = protocol}); } catch (const ReplayError& error) { return std::string(error.what()); }
        return std::string("accepted");
    };
    const auto good = match.recorder.bytes();

    AssertStrEq(message(good, 5).c_str(), "accepted", "the right protocol");
    Assert(message(good, 6).find("another protocol") != std::string::npos, "another protocol: %s", message(good, 6).c_str());
    ReplicationRegistry other;

    registerTransform2D(other, Replicate::Interpolated);
    bool refused = false;

    try { Replayer(other, good, screen.client, {.protocol = 5}); } catch (const ReplayError& error) { refused = std::string(error.what()).find("other components") != std::string::npos; }
    Assert(refused, "other components than the recording's (a new version of the game)");
    Assert(message({1, 2, 3}, 5).find("not a replay") != std::string::npos, "not a replay: %s", message({1, 2, 3}, 5).c_str());
    auto cut = good;

    cut.resize(cut.size() / 2);
    Assert(message(cut, 5).find("replay") != std::string::npos, "a file that is cut: %s", message(cut, 5).c_str());
    const auto file = std::filesystem::temp_directory_path() / "kuge_test.replay";

    match.recorder.save(file);
    kw::World world;
    ReplicationClient client(world, screen.registry);
    auto loaded = Replayer::load(screen.registry, file, client, {.protocol = 5});

    Assert(loaded.lastTick() == Replayer(screen.registry, good, screen.client, {.protocol = 5}).lastTick(), "saved and loaded again");
    std::filesystem::remove(file);
    bool missing = false;

    try { Replayer::load(screen.registry, "/nonexistent/x.replay", client, {.protocol = 5}); } catch (const std::exception&) { missing = true; }
    Assert(missing, "a file that is not there is an error");
}
