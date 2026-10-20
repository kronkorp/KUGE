extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Engine.hpp"
#include "EngineCommands.hpp"
#include "Logger.hpp"
#include "Profiler.hpp"
#include "Stage.hpp"
#include <atomic>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <thread>

// NOTE: kronklab test names are limited to 31 characters.
// The Profiler is a process-wide thing: kronklab runs each test in a process of its own.

namespace
{
    std::atomic<std::uint64_t> g_now{1000};   // the fake clock, in nanoseconds

    void useFakeClock(void)
    {
        g_now = 1000;
        kuge::Profiler::instance().setClock([] { return g_now.load(); });
    }

    // A system that takes 5 ms on the fake clock
    struct Slow : kw::ISystem
    {
        bool handle(kw::World&) override { g_now += 5'000'000; return true; }
    };

    struct Quick : kw::ISystem
    {
        bool handle(kw::World&) override { g_now += 1'000'000; return true; }
    };

    struct Level : kuge::Scene
    {
        void onEnter(void) override
        {
            addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Slow>());
            addSystem(kw::Schedule::Fixed, kuge::stage::Physics, std::make_unique<Quick>());
            addSystem(kw::Schedule::Frame, kuge::stage::Render, std::make_unique<Quick>());
        }
    };

    bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
}

Test(profiler, every_system_is_timed)
{
    useFakeClock();
    kuge::Engine engine;

    engine.scenes().change<Level>();
    engine.step(0.0);
    kuge::Profiler::instance().enable(true);
    engine.step(1.0 / 60.0);   // one tick, one frame
    kuge::Profiler::instance().enable(false);
    const auto events = kuge::Profiler::instance().events();
    const kuge::Profiler::Event* slow = nullptr;
    int quick = 0;

    for (const auto& event : events) {
        if (has(event.name, "Slow")) {
            slow = &event;
        }
        quick += has(event.name, "Quick") ? 1 : 0;
    }
    Assert(slow != nullptr, "the system is there, by its class name");
    Assert(slow && slow->end - slow->start == 5'000'000, "with its time: %llu ns", slow ? static_cast<unsigned long long>(slow->end - slow->start) : 0ull);
    Assert(slow && std::string(slow->category) == "Fixed/Simulation", "in the category of its stage: %s", slow ? slow->category : "-");
    AssertEq(quick, 2, "the other two systems too (a fixed one and a frame one)");
    bool loop = false;

    for (const auto& event : events) {
        loop = loop || std::string(event.name) == "tick" || std::string(event.name) == "frame";
    }
    Assert(loop, "and the tick and the frame of the loop");
}

Test(profiler, off_records_nothing)
{
    useFakeClock();
    kuge::Engine engine;

    engine.scenes().change<Level>();
    engine.step(0.0);
    engine.step(1.0 / 60.0);
    { KUGE_PROFILE_SCOPE("a scope"); }
    AssertEq(kuge::Profiler::instance().events().size(), 0u, "profiling is off: nothing");
    kuge::Profiler::instance().enable(true);
    { KUGE_PROFILE_SCOPE("a scope"); g_now += 2000; }
    const auto events = kuge::Profiler::instance().events();

    Assert(events.size() == 1 && std::string(events[0].name) == "a scope" && events[0].end - events[0].start == 2000, "on: one event with its time");
    kuge::Profiler::instance().clear();
    AssertEq(kuge::Profiler::instance().events().size(), 0u, "clear forgets");
}

Test(profiler, threads_do_not_mix)
{
    useFakeClock();
    kuge::Profiler::instance().enable(true);
    std::vector<std::thread> threads;

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([t] {
            const char* name = kuge::Profiler::intern("thread " + std::to_string(t));

            for (int i = 0; i < 100; ++i) {
                const std::uint64_t start = g_now.fetch_add(10);

                kuge::Profiler::instance().record(name, "test", start, start + 5);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto events = kuge::Profiler::instance().events();
    std::map<std::string, std::set<std::uint32_t>> threadsOf;

    AssertEq(events.size(), 400u, "every event of every thread");
    for (const auto& event : events) {
        threadsOf[event.name].insert(event.thread);
    }
    AssertEq(threadsOf.size(), 4u, "four names");
    for (const auto& [name, ids] : threadsOf) {
        Assert(ids.size() == 1, "'%s' is on one thread only", name.c_str());
    }
    Assert(kuge::Profiler::intern("thread 1") == kuge::Profiler::intern("thread 1"), "the same text, the same pointer");
}

Test(profiler, the_buffer_wraps)
{
    useFakeClock();
    kuge::Profiler::instance().enable(true);
    for (int i = 0; i < 40000; ++i) {
        kuge::Profiler::instance().record("event", "test", static_cast<std::uint64_t>(i), static_cast<std::uint64_t>(i) + 1);
    }
    const auto events = kuge::Profiler::instance().events();

    AssertEq(events.size(), 32768u, "the last 32768 are kept");
    Assert(events.front().start == 40000 - 32768 && events.back().start == 39999, "the oldest went, in order: %llu to %llu",
        static_cast<unsigned long long>(events.front().start), static_cast<unsigned long long>(events.back().start));
}

Test(profiler, the_trace_is_json)
{
    useFakeClock();
    kuge::Profiler::instance().enable(true);
    kuge::Profiler::instance().record("plain", "cat", 1000, 3000);
    kuge::Profiler::instance().record(kuge::Profiler::intern("quote \" back \\ tab \t"), "cat", 4000, 4500);
    const std::string json = kuge::Profiler::instance().traceJson();
    std::size_t events = 0;

    for (std::size_t at = json.find("\"ph\":\"X\""); at != std::string::npos; at = json.find("\"ph\":\"X\"", at + 1)) {
        ++events;
    }
    AssertEq(events, 2u, "one event per scope");
    Assert(json.front() == '[' && json.rfind(']') != std::string::npos, "an array");
    Assert(has(json, "\"name\":\"quote \\\" back \\\\ tab \\t\""), "quotes, backslashes and tabs are escaped: %s", json.c_str());
    Assert(has(json, "\"ts\":1.0") && has(json, "\"dur\":2.0"), "microseconds");
    Assert(has(json, "\"ph\":\"X\",\"ts\":4.0"), "a second event");
    const auto file = std::filesystem::temp_directory_path() / "kuge_profile_test.json";

    kuge::Profiler::instance().saveTrace(file);
    Assert(std::filesystem::file_size(file) == json.size(), "the file has the trace");
    std::filesystem::remove(file);
    bool threw = false;

    try { kuge::Profiler::instance().saveTrace("/nonexistent_folder/trace.json"); } catch (const std::runtime_error&) { threw = true; }
    Assert(threw, "a file that cannot be written is an error");
}

Test(profiler, the_slowest_names)
{
    useFakeClock();
    kuge::Profiler::instance().enable(true);
    kuge::Profiler::instance().record("small", "c", 0, 10);
    kuge::Profiler::instance().record("big", "c", 100, 400);
    kuge::Profiler::instance().record("big", "c", 500, 600);
    kuge::Profiler::instance().record("middle", "c", 700, 760);
    const auto rows = kuge::Profiler::instance().top(2);

    AssertEq(rows.size(), 2u, "the top two");
    Assert(rows[0].name == "big" && rows[0].calls == 2 && rows[0].total == 400 && rows[0].worst == 300, "big: 2 calls, 400 ns, worst 300");
    AssertStrEq(rows[1].name.c_str(), "middle", "then the next");
    AssertEq(kuge::Profiler::instance().top(5, 650).size(), 1u, "since a time: only what began after");
}

Test(stats, frames_per_second)
{
    kuge::FrameStats stats;

    for (int i = 0; i < 99; ++i) {
        stats.push(0.010, 0.001, 0.004);
    }
    stats.push(0.100, 0.002, 0.050);   // one slow frame in a hundred
    Assert(std::fabs(stats.frameMean() - 0.0109) < 1e-6, "mean %f", stats.frameMean());
    Assert(std::fabs(stats.fps() - 1.0 / 0.0109) < 0.01, "fps");
    Assert(std::fabs(stats.frameWorstPercent() - 0.100) < 1e-9, "the worst 1 % of a hundred frames is that one: %f", stats.frameWorstPercent());
    AssertEq(stats.frameWorst(), 0.100, "worst");
    Assert(std::fabs(stats.tickMean() - 0.00101) < 1e-6, "the tick");
    AssertEq(stats.recent(3).size(), 3u, "the last frames for a graph");
    AssertEq(stats.recent(3).back(), 0.100, "newest last");
    for (int i = 0; i < 1000; ++i) {
        stats.push(0.02, 0, 0.01);
    }
    AssertEq(stats.frames(), 1100u, "counts every frame");
    Assert(std::fabs(stats.frameMean() - 0.02) < 1e-9, "the mean is over the window only");
    Assert(kuge::describe(kuge::NetReport{}).find("rtt -") == 0, "a report with no rtt yet says so");
}
