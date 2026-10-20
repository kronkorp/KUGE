extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Timeline.hpp"
#include <cmath>
#include <fstream>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    std::string refused(const std::string& text)
    {
        try { kuge::Timeline::fromJson(kuge::Json::parse(text), "boss.json"); } catch (const kuge::TimelineError& error) { return error.what(); }
        return "accepted";
    }

    // A move of 2 seconds from 0.5, an event at 1 and one at 5, a line from 1 to 4
    const char* const BOSS = R"({ "duration": 6, "tracks": [
        { "kind": "event", "keys": [ { "t": 5, "name": "late" }, { "t": 1, "name": "early" }, { "t": 1, "name": "also_early" } ] },
        { "kind": "transform", "keys": [ { "t": 0.5, "duration": 2, "ease": "inQuad", "target": "boss", "to": { "position": [10, 20] } } ] },
        { "kind": "text", "keys": [ { "t": 1, "duration": 3, "speaker": "Boss", "text": "You came." } ] } ] })";

    std::shared_ptr<const kuge::Timeline> boss(void)
    {
        return std::make_shared<kuge::Timeline>(kuge::Timeline::fromJson(kuge::Json::parse(BOSS), "boss.json"));
    }

    // What a player did, as text: "1.00 start event/early", "tween transform 0.50"...
    struct Log
    {
        std::vector<std::string> lines;
        double                   now = 0.0;

        kuge::TimelinePlayer::Callback callback(void)
        {
            return [this](const kuge::TimelinePlayer::Step& step) {
                static const char* const phases[] = {"start", "tween", "end"};
                std::string line = std::string(phases[static_cast<int>(step.phase)]) + " " + kuge::trackKindName(step.track.kind);

                if (step.track.kind == kuge::TrackKind::Event) {
                    line += "/" + step.key.data["name"].string();
                }
                if (step.phase == kuge::TimelinePlayer::Step::Phase::Tween) {
                    line += " " + std::to_string(static_cast<int>(std::lround(step.progress * 100)));
                }
                lines.push_back(line);
            };
        }
        std::string joined(void) const
        {
            std::string all;

            for (const auto& line : lines) {
                all += line + "; ";
            }
            return all;
        }
        std::size_t count(const std::string& what) const
        {
            std::size_t n = 0;

            for (const auto& line : lines) {
                n += line == what;
            }
            return n;
        }
    };
}

Test(timeline, curves_start_and_end)
{
    for (auto curve : {kuge::Ease::Linear, kuge::Ease::InQuad, kuge::Ease::OutQuad, kuge::Ease::InOutQuad}) {
        Assert(kuge::ease(curve, 0.0) == 0.0 && kuge::ease(curve, 1.0) == 1.0, "a curve goes from 0 to 1");
        Assert(kuge::ease(curve, -3.0) == 0.0 && kuge::ease(curve, 7.0) == 1.0, "and is clamped");
    }
    Assert(kuge::ease(kuge::Ease::InQuad, 0.5) == 0.25, "in is slow at the start");
    Assert(kuge::ease(kuge::Ease::OutQuad, 0.5) == 0.75, "out is slow at the end");
    Assert(kuge::ease(kuge::Ease::InOutQuad, 0.5) == 0.5, "in-out is half at half");
}

Test(timeline, keys_start_in_order)
{
    Log log;
    kuge::TimelinePlayer player(boss(), log.callback());

    player.update(0.4);
    AssertEq(log.lines.size(), 0u, "nothing before the first key");
    player.update(0.2);   // 0.6: the move began
    Assert(log.joined() == "start transform; tween transform 5; ", "the move began at 0.5 and is 5%% in: %s", log.joined().c_str());
    log.lines.clear();
    player.update(0.5);   // 1.1
    Assert(log.joined() == "start event/early; start event/also_early; start text; tween transform 30; tween text 3; ",
        "by time, then track, then place: %s", log.joined().c_str());
    log.lines.clear();
    player.update(10.0);
    Assert(player.finished() && player.time() == 6.0, "the end is the duration");
    AssertEq(log.count("end transform"), 1u, "the move ended once");
    AssertEq(log.count("end text"), 1u, "so did the line");
    AssertEq(log.count("start event/late"), 1u, "and the late event came once");
    player.update(1.0);
    AssertEq(log.count("start event/late"), 1u, "a finished player does nothing");
}

Test(timeline, frame_size_is_no_matter)
{
    Log coarse, fine;
    kuge::TimelinePlayer a(boss(), coarse.callback()), b(boss(), fine.callback());

    a.update(7.0);
    for (int i = 0; i < 700 && !b.finished(); ++i) {
        b.update(0.01);
    }
    for (const char* thing : {"start event/early", "start event/also_early", "start event/late", "start text", "end text", "start transform", "end transform"}) {
        AssertEq(coarse.count(thing), fine.count(thing), "%s happens as many times", thing);
        AssertEq(fine.count(thing), 1u, "once: %s", thing);
    }
}

Test(timeline, skip_gives_the_same_end)
{
    Log played, skipped;
    kuge::TimelinePlayer a(boss(), played.callback()), b(boss(), skipped.callback());

    while (!a.finished()) {
        a.update(0.25);
    }
    b.update(0.75);
    skipped.lines.clear();
    b.skip();
    Assert(b.finished(), "skip finishes it");
    Assert(skipped.joined() == "start event/early; start event/also_early; start text; end transform; end text; start event/late; ",
        "what was left happens in order of time: %s", skipped.joined().c_str());
    Log all;
    kuge::TimelinePlayer c(boss(), all.callback());

    c.skip();
    AssertEq(all.count("start event/early") + all.count("start event/late") + all.count("end transform") + all.count("end text"), 4u,
        "even from the very start: every event once and every tween ended");
    b.skip();
    AssertEq(skipped.count("start event/late"), 1u, "a second skip does nothing");
}

Test(timeline, an_end_comes_before_a_start)
{
    Log log;
    const auto timeline = std::make_shared<kuge::Timeline>(kuge::Timeline::fromJson(kuge::Json::parse(R"({ "duration": 4, "tracks": [
        { "kind": "fade", "keys": [ { "t": 0, "duration": 2, "color": [1, 0, 0, 1] }, { "t": 2, "color": [0, 0, 0, 0] } ] } ] })"), "fade.json"));
    kuge::TimelinePlayer player(timeline, log.callback());

    player.update(2.5);
    Assert(log.joined() == "start fade; end fade; start fade; ", "the first fade ends before the second starts, though they are in one update: %s", log.joined().c_str());
}

Test(timeline, pause_speed_and_seek)
{
    Log log;
    kuge::TimelinePlayer player(boss(), log.callback());

    player.pause();
    player.update(3.0);
    AssertEq(player.time(), 0.0, "a paused player does not move");
    player.play();
    player.setSpeed(2.0);
    player.update(1.0);
    AssertEq(player.time(), 2.0, "twice as fast");
    player.setSpeed(-5.0);
    player.update(1.0);
    AssertEq(player.time(), 2.0, "speed is not negative");
    player.setSpeed(1.0);
    log.lines.clear();
    player.seek(1.5);
    AssertEq(log.count("start event/early"), 1u, "going back plays again up to there: the early event starts again");
    AssertEq(log.count("start event/late"), 0u, "but not what is after");
    AssertEq(player.time(), 1.5, "at that time");
    player.seek(100.0);
    Assert(player.finished(), "seek is clamped to the end");
    player.restart();
    Assert(!player.finished() && player.time() == 0.0, "restart");
}

Test(timeline, wrong_files_are_named)
{
    const auto why = [](const char* tracks) { return refused(std::string(R"({"duration": 5, "tracks": [)") + tracks + "]}"); };

    Assert(why(R"({"kind": "teleport", "keys": []})").find("boss.json: tracks[0]: unknown kind 'teleport'") == 0, "an unknown kind: %s", why(R"({"kind": "teleport", "keys": []})").c_str());
    Assert(why(R"({"kind": "event", "keys": [{"t": -1, "name": "x"}]})").find("boss.json: tracks[0].keys[0]: 't' cannot be negative") == 0, "a negative time");
    Assert(why(R"({"kind": "event", "keys": [{"t": 1, "name": "ok"}, {"t": 9, "name": "x"}]})").find("tracks[0].keys[1]") != std::string::npos, "a key after the end, and which one");
    Assert(why(R"({"kind": "transform", "keys": [{"t": 4, "duration": 2, "target": "a", "to": {"x": 1}}]})").find("ends at 6") != std::string::npos, "a tween that ends after the end");
    Assert(why(R"({"kind": "event", "keys": [{"t": 1}]})").find("'name' is missing") != std::string::npos, "a missing field");
    Assert(why(R"({"kind": "text", "keys": [{"t": 1, "text": 5}]})").find("'text' must be a string") != std::string::npos, "a field of the wrong type");
    Assert(why(R"({"kind": "fade", "keys": [{"t": 1, "color": [0, 0, 0]}]})").find("'color' must be 4 numbers") != std::string::npos, "a colour with 3 numbers");
    Assert(why(R"({"kind": "camera", "keys": [{"t": 1}]})").find("needs 'position', 'zoom' or 'shake'") != std::string::npos, "a camera key that does nothing");
    Assert(why(R"({"kind": "event", "keys": [{"t": 1, "name": "x", "ease": "bounce"}]})").find("unknown ease 'bounce'") != std::string::npos, "an unknown ease");
    Assert(why(R"({"kind": "audio", "keys": [{"t": 1}]})").find("needs 'sound', 'music' or 'stopMusic'") != std::string::npos, "an audio key with no sound");
    Assert(refused(R"({"tracks": []})").find("'duration' is missing") != std::string::npos, "no duration");
    Assert(refused(R"({"duration": 0, "tracks": []})").find("above 0") != std::string::npos, "a duration of 0");
    Assert(refused("[]").find("boss.json: a timeline is an object") == 0, "not an object");
    Assert(refused(R"({"duration": 5, "tracks": []})") == "accepted", "an empty timeline is fine");
}

Test(timeline, a_file_names_itself)
{
    const std::string path = "/tmp/kuge_timeline_" + std::to_string(::getpid()) + ".json";
    std::string message;

    std::ofstream(path) << BOSS;
    const kuge::Timeline loaded = kuge::Timeline::load(path);

    AssertStrEq(loaded.id.c_str(), ("kuge_timeline_" + std::to_string(::getpid())).c_str(), "the id is the file name");
    Assert(loaded.tracks.size() == 3 && loaded.tracks[0].keys[0].data["name"].string() == "early", "keys are sorted by time (the file had 'late' first)");
    std::ofstream(path) << "{ \"duration\": ";
    try { kuge::Timeline::load(path); } catch (const kuge::TimelineError& error) { message = error.what(); }
    Assert(message.find("kuge_timeline_") == 0 && message.find("line 1") != std::string::npos, "a broken file says its name and where: %s", message.c_str());
    std::remove(path.c_str());
    message.clear();
    try { kuge::Timeline::load(path); } catch (const kuge::TimelineError& error) { message = error.what(); }
    Assert(!message.empty() && message.find("kuge_timeline_") == 0, "a missing file too: %s", message.c_str());
}
