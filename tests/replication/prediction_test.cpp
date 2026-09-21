extern "C" {
    #include "kronklab/kronklab.h"
}
#include "prediction_fixture.hpp"
#include <algorithm>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using kuge::net::LoopbackNetwork;
}

Test(prediction, the_same_inputs)
{
    // Same inputs, same positions: the client simulates what the server simulates, so the server never contradicts it
    PredSim sim;
    const auto steers = PredSim::script(800, 5);

    sim.play(steers);
    sim.rest(60);
    Assert(distanceBetween(sim.clientPosition(), sim.serverPosition()) < 0.01f, "the client and the server agree: %f", distanceBetween(sim.clientPosition(), sim.serverPosition()));
    AssertEq(sim.prediction->stats().corrections, 0, "and the server never corrected the client (%llu snapshots used)", static_cast<unsigned long long>(sim.prediction->stats().reconciliations));
    Assert(sim.prediction->stats().reconciliations > 100, "although it was consulted a lot");
    Assert(sim.serverPosition().x > 60.0f || sim.serverPosition().y > 60.0f || sim.serverPosition().x < 40.0f || sim.serverPosition().y < 40.0f, "and the player did move: (%f, %f)", sim.serverPosition().x, sim.serverPosition().y);
}

Test(prediction, answers_at_once)
{
    // 100 ms each way: the server knows nothing of this input for a long time, and the client already shows it
    PredSim sim(LoopbackNetwork::Conditions{.latency = 0.1});

    sim.rest(120);
    Assert(sim.prediction->ready(), "the player's entity is there");
    const float before = sim.clientPosition().x;

    sim.step(Steer{1, 0});
    AssertEq(sim.clientPosition().x - before, STEP, "the very tick it presses, the player moved by one step");
    Assert(sim.serverPosition().x < before + 0.001f, "while the server has not heard yet");
    sim.step(Steer{1, 0});
    AssertEq(sim.clientPosition().x - before, 2 * STEP, "and again");
}

Test(prediction, walls_stop_it_at_once)
{
    // The prediction includes the level: the player stops at the wall without waiting for the server
    PredSim sim(LoopbackNetwork::Conditions{.latency = 0.05});

    sim.rest(60);
    for (int t = 0; t < 200; ++t) {
        sim.step(Steer{1, 0});
    }
    // The wall at x = 300 is 20 wide: its face is at 290, the player is 10 wide: it stops at 285
    Assert(std::fabs(sim.clientPosition().x - 285.0f) < 0.1f, "stopped by the wall: %f", sim.clientPosition().x);
    sim.rest(60);
    Assert(std::fabs(sim.serverPosition().x - 285.0f) < 0.1f, "in the same place as the server: %f", sim.serverPosition().x);
}

Test(prediction, over_a_slow_network)
{
    // 100 ms of round trip, jitter and loss: the player still ends where the server has it, and moves smoothly
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        PredSim sim(LoopbackNetwork::Conditions{.loss = 0.1, .duplicate = 0.05, .latency = 0.05, .jitter = 0.02, .seed = seed});
        const auto steers = PredSim::script(900, static_cast<unsigned>(seed));

        sim.play(steers);
        sim.rest(120);
        Assert(distanceBetween(sim.clientPosition(), sim.serverPosition()) < 0.05f, "seed %llu: it ends where the server has it: %f", static_cast<unsigned long long>(seed), distanceBetween(sim.clientPosition(), sim.serverPosition()));
        // At most one step (2 pixels) a tick, and what the corrections add: no jump
        const float longest = longestStep(sim.shown, 60);

        Assert(longest < 3.5f, "seed %llu: the drawn player never jumps: the longest step is %f pixels", static_cast<unsigned long long>(seed), longest);
        AssertEq(sim.prediction->stats().snaps, 0, "seed %llu: never put back by force", static_cast<unsigned long long>(seed));
        Assert(sim.prediction->stats().corrections * 20 < sim.prediction->stats().reconciliations, "seed %llu: the prediction is right nearly always: %llu corrections in %llu snapshots",
            static_cast<unsigned long long>(seed), static_cast<unsigned long long>(sim.prediction->stats().corrections), static_cast<unsigned long long>(sim.prediction->stats().reconciliations));
    }
}

Test(prediction, a_wall_it_cannot_see)
{
    // A wall that only the server has moves into the player: the client cannot predict it, and is corrected without a jump
    PredSim sim(LoopbackNetwork::Conditions{.latency = 0.05, .jitter = 0.01, .seed = 3});

    sim.addMover();
    sim.rest(60);
    for (int t = 0; t < 400; ++t) {
        sim.step(Steer{static_cast<std::int8_t>(t % 200 < 100 ? 1 : -1), 0});     // walks to and fro across the wall's path
    }
    sim.rest(120);
    const auto& stats = sim.prediction->stats();

    Assert(stats.corrections > 0, "the server did contradict the client: %llu corrections (largest %f)", static_cast<unsigned long long>(stats.corrections), stats.largestCorrection);
    Assert(distanceBetween(sim.clientPosition(), sim.serverPosition()) < 0.05f, "and in the end they agree: %f", distanceBetween(sim.clientPosition(), sim.serverPosition()));
    const float longest = longestStep(sim.shown, 60);

    Assert(longest < 5.0f, "each correction is spread over several ticks: the longest step drawn is %f", longest);
    AssertEq(stats.snaps, 0, "none was a teleport");
}

Test(prediction, smoothing_is_needed)
{
    // The same wall, without smoothing: the corrections show as jumps (this is what smoothing is for)
    kuge::replication::PredictionConfig<Steer> raw;

    raw.smoothing = 1e-6;                 // absorbed at once
    PredSim sim(LoopbackNetwork::Conditions{.latency = 0.05, .jitter = 0.01, .seed = 3}, 0, raw);

    sim.addMover();
    sim.rest(60);
    for (int t = 0; t < 400; ++t) {
        sim.step(Steer{static_cast<std::int8_t>(t % 200 < 100 ? 1 : -1), 0});
    }
    Assert(longestStep(sim.shown, 60) > 5.0f, "with no smoothing the player jumps: %f", longestStep(sim.shown, 60));
}

Test(prediction, too_far_is_a_snap)
{
    PredSim sim(LoopbackNetwork::Conditions{.latency = 0.02});

    sim.rest(90);
    sim.serverWorld.get<kuge::Transform2D>(sim.serverPlayer).position = {200.0f, 200.0f};      // the server moves it: a respawn, a teleporter
    sim.rest(30);
    AssertEq(sim.prediction->stats().snaps, 1, "one snap");
    Assert(distanceBetween(sim.clientPosition(), {200.0f, 200.0f}) < 0.1f, "the player is where the server put it, at once: (%f, %f)", sim.clientPosition().x, sim.clientPosition().y);
}

Test(prediction, a_whole_run_repeats)
{
    auto run = [] {
        PredSim sim(LoopbackNetwork::Conditions{.loss = 0.15, .duplicate = 0.05, .latency = 0.04, .jitter = 0.02, .seed = 9});

        sim.addMover();
        sim.play(PredSim::script(600, 4));
        sim.rest(60);
        return sim.shown;
    };
    const auto first = run();
    const auto second = run();

    Assert(first.size() == second.size(), "same length");
    bool same = true;

    for (std::size_t i = 0; same && i < first.size(); ++i) {
        same = first[i] == second[i];
    }
    Assert(same, "the same network and the same inputs: the same positions, tick for tick, to the last bit");
}

Test(prediction, inputs_survive_loss)
{
    // 30% of the packets are lost each way: each carries the last four inputs, so almost none is missing
    PredSim sim(LoopbackNetwork::Conditions{.loss = 0.3, .latency = 0.03, .jitter = 0.01, .seed = 6});
    const auto steers = PredSim::script(800, 2);

    sim.play(steers);
    sim.rest(60);
    const auto& stats = sim.inputs->stats();
    const std::uint32_t sent = sim.prediction->sequence();

    Assert(stats.received > sent * 95 / 100, "%llu of the %u inputs reached the server", static_cast<unsigned long long>(stats.received), sent);
    Assert(stats.duplicates > 0, "the redundancy shows: %llu copies", static_cast<unsigned long long>(stats.duplicates));
    Assert(distanceBetween(sim.clientPosition(), sim.serverPosition()) < 0.05f, "and they agree at the end");
}

Test(prediction, a_jitter_buffer)
{
    // A buffer of two inputs on the server keeps it from repeating the last one when one is a little late
    for (const std::size_t jitter : {std::size_t{0}, std::size_t{2}}) {
        PredSim sim(LoopbackNetwork::Conditions{.latency = 0.03, .jitter = 0.04, .seed = 12}, jitter);

        sim.play(PredSim::script(600, 7));
        sim.rest(60);
        Assert(distanceBetween(sim.clientPosition(), sim.serverPosition()) < 0.05f, "jitter %zu: they agree at the end", jitter);
        std::printf("    (jitter buffer %zu: %llu inputs repeated)\n", jitter, static_cast<unsigned long long>(sim.inputs->stats().filled));
    }
}

// -- The inputs on the server, alone ------------------------------------------------------------
namespace
{
    kuge::replication::InputPacket packetOf(std::uint32_t first, const std::vector<Steer>& steers)
    {
        kuge::replication::InputPacket packet;

        packet.firstSequence = first;
        packet.count = static_cast<std::uint8_t>(steers.size());
        for (const Steer& steer : steers) {
            kuge::ByteWriter one;

            kuge::net::encode(one, steer);
            packet.data.push_back(static_cast<std::uint8_t>(one.size() & 0xFF));
            packet.data.push_back(static_cast<std::uint8_t>(one.size() >> 8));
            packet.data.insert(packet.data.end(), one.bytes().begin(), one.bytes().end());
        }
        return packet;
    }

    struct InputBench
    {
        double now = 0.0;
        kuge::net::LoopbackNetwork network{{}, [this] { return now; }};
        std::unique_ptr<kuge::net::Endpoint> server, client;
        std::unique_ptr<kuge::replication::InputServer<Steer>> inputs;
        kuge::net::ConnectionId id = 0;

        explicit InputBench(kuge::replication::InputServerConfig config = {})
        {
            kuge::net::EndpointConfig ec;

            ec.clock = [this] { return now; };
            server = std::make_unique<kuge::net::Endpoint>(network.listen("in"), kuge::net::Role::Server, ec);
            client = std::make_unique<kuge::net::Endpoint>(network.connect("in"), kuge::net::Role::Client, ec);
            inputs = std::make_unique<kuge::replication::InputServer<Steer>>(*server, config);
            for (int i = 0; i < 200 && !(client->connected() && server->connected()); ++i) {
                now += 0.01;
                server->poll();
                client->poll();
            }
            id = server->connections().front();
            inputs->addClient(id);
        }

        void send(const kuge::replication::InputPacket& packet)
        {
            client->send(kuge::net::CLIENT_CONNECTION, packet, kuge::net::Channel::Unreliable);
            now += 0.01;
            server->poll();
        }

        std::vector<kuge::replication::InputServer<Steer>::Applied> tick(void)
        {
            now += 0.01;
            server->poll();
            return inputs->collect();
        }
    };
}

Test(inputserver, in_order_one_a_tick)
{
    InputBench bench;

    bench.send(packetOf(1, {Steer{1, 0}, Steer{0, 1}, Steer{-1, 0}}));
    for (std::uint32_t expected = 1; expected <= 3; ++expected) {
        const auto applied = bench.tick();

        Assert(applied.size() == 1 && applied[0].sequence == expected && !applied[0].repeated, "tick %u applies input %u", expected, expected);
    }
}

Test(inputserver, copies_are_ignored)
{
    InputBench bench;

    bench.send(packetOf(1, {Steer{1, 0}, Steer{0, 1}}));
    bench.send(packetOf(1, {Steer{1, 0}, Steer{0, 1}, Steer{1, 1}}));      // the redundancy of the next packet
    AssertEq(bench.inputs->stats().received, 3, "three different inputs");
    AssertEq(bench.inputs->stats().duplicates, 2, "two copies");
    std::vector<std::uint32_t> got;

    for (int i = 0; i < 3; ++i) {
        for (const auto& applied : bench.tick()) { got.push_back(applied.sequence); }
    }
    Assert(got == std::vector<std::uint32_t>({1, 2, 3}), "each applied once");
}

Test(inputserver, a_lost_input)
{
    InputBench bench;

    bench.send(packetOf(1, {Steer{1, 0}, Steer{1, 0}}));
    bench.send(packetOf(4, {Steer{0, 1}, Steer{0, 1}}));     // 3 never came
    std::vector<std::pair<std::uint32_t, bool>> got;

    for (int i = 0; i < 5; ++i) {
        for (const auto& applied : bench.tick()) { got.emplace_back(applied.sequence, applied.repeated); }
    }
    // 1, 2, then 3 is given to the last input again, then 4, 5
    const std::vector<std::pair<std::uint32_t, bool>> expected = {{1, false}, {2, false}, {3, true}, {4, false}, {5, false}};

    Assert(got.size() >= 5 && std::equal(expected.begin(), expected.end(), got.begin()), "a lost input takes the place of the last one, and the order goes on");
    bench.send(packetOf(3, {Steer{0, 0}}));
    AssertEq(bench.inputs->stats().duplicates, 1, "and if it comes after all, it is thrown away");
}

Test(inputserver, a_late_client)
{
    InputBench bench;

    bench.send(packetOf(1, {Steer{1, 0}}));
    AssertEq(bench.tick().size(), 1, "the input");
    const auto second = bench.tick();     // nothing new
    const auto third = bench.tick();

    Assert(second.size() == 1 && second[0].repeated && second[0].sequence == 1, "with nothing new, the last one goes on (and the number stays)");
    Assert(third.size() == 1 && third[0].repeated && third[0].sequence == 1, "and again");
    bench.send(packetOf(2, {Steer{0, 1}}));
    const auto fourth = bench.tick();

    Assert(fourth.size() == 1 && fourth[0].sequence == 2 && !fourth[0].repeated, "then the new input, with its number");
    AssertEq(bench.inputs->stats().filled, 2, "two ticks were filled");
}

Test(inputserver, a_jitter_buffer_waits)
{
    InputBench bench(kuge::replication::InputServerConfig{.jitter = 2});

    bench.send(packetOf(1, {Steer{1, 0}}));
    bench.send(packetOf(2, {Steer{1, 0}}));
    const auto waiting = bench.tick();

    Assert(waiting.size() == 1 && waiting[0].repeated, "with two inputs in the queue, the third must be there before it applies the first");
    bench.send(packetOf(3, {Steer{1, 0}}));
    const auto applied = bench.tick();

    Assert(applied.size() == 1 && !applied[0].repeated && applied[0].sequence == 1, "and now the first goes");
}

Test(inputserver, junk_and_limits)
{
    InputBench bench(kuge::replication::InputServerConfig{.maxQueued = 5});
    kuge::replication::InputPacket junk;

    junk.firstSequence = 1;
    junk.count = 3;
    junk.data = {2, 0, 1};                // says 2 bytes, has 1
    bench.send(junk);
    AssertEq(bench.inputs->stats().malformed, 1, "junk is counted");
    junk.data = {200, 0, 1, 2};
    bench.send(junk);
    AssertEq(bench.inputs->stats().malformed, 2, "a length that goes beyond the data too");
    std::vector<Steer> many(20, Steer{1, 1});

    bench.send(packetOf(1, many));
    Assert(bench.inputs->stats().dropped > 0, "a client that sends far more than is applied: the oldest are dropped");
    AssertEq(bench.tick().size(), 1, "and the server is alive");
}
