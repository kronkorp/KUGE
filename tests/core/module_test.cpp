extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Engine.hpp"
#include "Ref.hpp"
#include "Stage.hpp"
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    std::vector<std::string> g_events;

    void note(const std::string& event)
    {
        g_events.push_back(event);
    }

    std::string events(void)
    {
        std::string joined;

        for (const auto& event : g_events) {
            joined += (joined.empty() ? "" : ",") + event;
        }
        return joined;
    }

    // Two different types, so that both can give a resource to the same World
    template<int N>
    class Probe : public kuge::Module
    {
        public:
            explicit Probe(bool stopEachFrame = false) : m_stop(stopEachFrame) {}
            ~Probe(void) override { note(std::to_string(N) + ":dtor"); }

            void onAttach(kuge::Engine&) override { note(std::to_string(N) + ":attach"); }
            void inject(kw::World& world) override
            {
                note(std::to_string(N) + ":inject");
                world.addResource<kuge::Ref<Probe<N>>>(*this);
            }
            void beginFrame(kuge::Engine& engine) override
            {
                note(std::to_string(N) + ":begin");
                if (m_stop) {
                    engine.stop();
                }
            }
            void endFrame(kuge::Engine&) override { note(std::to_string(N) + ":end"); }

        private:
            bool m_stop;
    };

    class FailsToAttach : public kuge::Module
    {
        public:
            ~FailsToAttach(void) override { note("failing:dtor"); }
            void onAttach(kuge::Engine&) override { throw std::runtime_error("no"); }
    };

    class Log : public kw::ISystem
    {
        public:
            explicit Log(const char* what) : m_what(what) {}
            bool handle(kw::World&) override { note(m_what); return true; }

        private:
            const char* m_what;
    };

    class Logging : public kuge::Scene
    {
        public:
            void onEnter(void) override
            {
                note("scene:enter");
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Log>("tick"));
                addSystem(kw::Schedule::Frame, kuge::stage::Render, std::make_unique<Log>("frame"));
            }
            void onExit(void) override { note("scene:exit"); }
            ~Logging(void) override { note("scene:dtor"); }
    };

    // Reads the module through its Ref, in the World of the scene
    class Reads : public kuge::Scene
    {
        public:
            explicit Reads(const void*& seen) : m_seen(seen) {}

            void onEnter(void) override
            {
                m_seen = &world().getResource<kuge::Ref<Probe<1>>>().get();
            }

        private:
            const void*& m_seen;
    };
}

Test(modules, add_and_find)
{
    kuge::Engine engine;

    g_events.clear();
    auto& probe = engine.addModule<Probe<1>>();
    AssertStrEq(events().c_str(), "1:attach", "attached once, got %s", events().c_str());
    Assert(engine.module<Probe<1>>() == &probe, "found by its type");
    Assert(engine.module<Probe<2>>() == nullptr, "another type is not there");
}

Test(modules, hooks_frame_the_loop)
{
    kuge::Engine engine;

    engine.addModule<Probe<1>>();
    engine.addModule<Probe<2>>();
    engine.scenes().change<Logging>();
    engine.step(0.0);
    g_events.clear();
    engine.step(2.0 / 60.0);
    AssertStrEq(events().c_str(), "1:begin,2:begin,tick,tick,frame,1:end,2:end",
        "before the ticks, after the frame, in order, got %s", events().c_str());
}

Test(modules, inject_before_on_enter)
{
    kuge::Engine engine;
    const void* seen = nullptr;

    auto& probe = engine.addModule<Probe<1>>();
    g_events.clear();
    engine.scenes().change<Reads>(std::ref(seen));
    engine.step(0.0);
    Assert(seen == &probe, "the scene reaches the module through its World");
    AssertStrEq(events().c_str(), "1:inject,1:begin,1:end", "injected once, on entering, got %s", events().c_str());
}

Test(modules, inject_order)
{
    kuge::Engine engine;

    engine.addModule<Probe<1>>();
    g_events.clear();
    engine.scenes().change<Logging>();
    engine.step(0.0);
    Assert(events().rfind("1:inject", 0) == 0, "injected first, got %s", events().c_str());
    Assert(events().find("1:inject") < events().find("scene:enter"), "before onEnter(), got %s", events().c_str());
}

Test(modules, gone_after_the_scenes)
{
    g_events.clear();
    {
        kuge::Engine engine;

        engine.addModule<Probe<1>>();
        engine.addModule<Probe<2>>();
        engine.scenes().change<Logging>();
        engine.step(0.0);
        g_events.clear();
    }
    AssertStrEq(events().c_str(), "scene:exit,scene:dtor,2:dtor,1:dtor",
        "scenes first, then the modules last added first, got %s", events().c_str());
}

Test(modules, begin_frame_can_stop)
{
    kuge::Engine engine;

    engine.addModule<Probe<1>>(true);
    engine.scenes().change<Logging>();
    g_events.clear();
    AssertEq(engine.step(2.0 / 60.0), false, "the engine is done");
    Assert(events().find("tick") == std::string::npos, "and nothing was simulated: %s", events().c_str());
}

Test(modules, failed_attach_adds_nothing)
{
    kuge::Engine engine;
    bool threw = false;

    g_events.clear();
    try {
        engine.addModule<FailsToAttach>();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Assert(threw, "the error comes out");
    Assert(engine.module<FailsToAttach>() == nullptr, "not added");
    AssertStrEq(events().c_str(), "failing:dtor", "and destroyed, got %s", events().c_str());
}

Test(modules, max_fps_needs_tick_rate)
{
    bool tooLow = false;

    try { kuge::Engine engine({.tickRate = 60, .maxFps = 30}); } catch (const std::invalid_argument&) { tooLow = true; }
    Assert(tooLow, "fewer frames than ticks makes no sense");
    kuge::Engine same({.tickRate = 60, .maxFps = 60});
    kuge::Engine faster({.tickRate = 60, .maxFps = 240});
    AssertEq(faster.config().maxFps, 240, "kept");
}

Test(modules, max_fps_gives_more_frames)
{
    class Count : public kw::ISystem
    {
        public:
            Count(std::uint64_t& n, kuge::Engine* stopAt) : m_n(n), m_engine(stopAt) {}
            bool handle(kw::World&) override
            {
                if (++m_n == 12 && m_engine) {
                    m_engine->stop();
                }
                return true;
            }
        private:
            std::uint64_t& m_n;
            kuge::Engine*  m_engine;
    };
    struct Scn : kuge::Scene
    {
        Scn(std::uint64_t& ticks, std::uint64_t& frames) : m_ticks(ticks), m_frames(frames) {}
        void onEnter(void) override
        {
            addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Count>(m_ticks, &ctx().engine()));
            addSystem(kw::Schedule::Frame, kuge::stage::Render, std::make_unique<Count>(m_frames, nullptr));
        }
        std::uint64_t& m_ticks;
        std::uint64_t& m_frames;
    };
    kuge::Engine engine({.tickRate = 60, .maxFps = 240});
    std::uint64_t ticks = 0;
    std::uint64_t frames = 0;
    const auto start = std::chrono::steady_clock::now();

    engine.scenes().change<Scn>(std::ref(ticks), std::ref(frames));
    engine.run();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    AssertEq(ticks, 12, "12 ticks, got %lu", (unsigned long)ticks);
    Assert(frames >= 2 * ticks, "several frames per tick: %lu frames for %lu ticks", (unsigned long)frames, (unsigned long)ticks);
    Assert(seconds < 1.0, "and still in real time: %f s", seconds);
}
