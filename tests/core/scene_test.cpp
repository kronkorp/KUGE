extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Engine.hpp"
#include "Stage.hpp"
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // What the scenes and systems of a test did, in order
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

    class Recorder : public kuge::Scene
    {
        public:
            explicit Recorder(std::string name) : m_name(std::move(name)) { note(m_name + ":ctor"); }
            ~Recorder(void) override { note(m_name + ":dtor"); }

            void onEnter(void) override { note(m_name + ":enter"); }
            void onExit(void) override { note(m_name + ":exit"); }
            void onPause(void) override { note(m_name + ":pause"); }
            void onResume(void) override { note(m_name + ":resume"); }

        private:
            std::string m_name;
    };

    // Asks for another scene the first time it ticks
    class Changer : public kuge::Scene
    {
        public:
            class Tick : public kw::ISystem
            {
                public:
                    explicit Tick(kuge::SceneContext& ctx) : m_ctx(ctx) {}

                    bool handle(kw::World&) override
                    {
                        note("tick");
                        if (!m_asked) {
                            m_asked = true;
                            m_ctx.scenes().change<Recorder>("next");
                            note("asked");
                        }
                        return true;
                    }

                private:
                    kuge::SceneContext& m_ctx;
                    bool                m_asked = false;
            };

            void onEnter(void) override
            {
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Tick>(ctx()));
            }
            void onExit(void) override { note("changer:exit"); }
    };

    class Noisy : public kw::ISystem
    {
        public:
            ~Noisy(void) override { note("system:dtor"); }
            bool handle(kw::World&) override { return true; }
    };

    class WithSystem : public kuge::Scene
    {
        public:
            void onEnter(void) override
            {
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Noisy>());
            }
            void onExit(void) override { note("scene:exit"); }
            ~WithSystem(void) override { note("scene:dtor"); }
    };

    struct Seen {
        std::vector<std::uint64_t> ticks;
        std::vector<double>        alphas;
        std::vector<double>        dts;
    };

    class ReadTime : public kw::ISystem
    {
        public:
            ReadTime(Seen& seen, bool fixed) : m_seen(seen), m_fixed(fixed) {}

            bool handle(kw::World& world) override
            {
                const auto& time = world.getResource<kuge::Time>();

                if (m_fixed) {
                    m_seen.ticks.push_back(time.tick);
                    m_seen.dts.push_back(time.dt);
                } else {
                    m_seen.alphas.push_back(time.alpha);
                }
                return true;
            }

        private:
            Seen& m_seen;
            bool  m_fixed;
    };

    class TimeScene : public kuge::Scene
    {
        public:
            explicit TimeScene(Seen& seen) : m_seen(seen) {}

            void onEnter(void) override
            {
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<ReadTime>(m_seen, true));
                addSystem(kw::Schedule::Frame, kuge::stage::Render, std::make_unique<ReadTime>(m_seen, false));
            }

        private:
            Seen& m_seen;
    };

    struct Args {
        int                  number = 0;
        std::string          text;
        int                  owned = 0;
        kuge::Engine*        engine = nullptr;
        std::uint32_t        tickRate = 0;
    };
    Args g_args;

    class WithArgs : public kuge::Scene
    {
        public:
            WithArgs(int number, std::string text, std::unique_ptr<int> owned)
            {
                g_args.number = number;
                g_args.text = std::move(text);
                g_args.owned = *owned;
            }

            void onEnter(void) override
            {
                g_args.engine = &ctx().engine();
                g_args.tickRate = ctx().time().tickRate;
            }
    };
}

Test(scene, enter_and_leave)
{
    kuge::Engine engine;

    g_events.clear();
    engine.scenes().change<Recorder>("A");
    AssertEq(g_events.size(), 0, "nothing happens before the engine runs a loop");
    engine.step(0.0);
    AssertStrEq(events().c_str(), "A:ctor,A:enter", "entered, got %s", events().c_str());
    engine.scenes().clear();
    AssertStrEq(events().c_str(), "A:ctor,A:enter,A:exit,A:dtor", "left, got %s", events().c_str());
    Assert(engine.scenes().empty(), "no scene left");
}

Test(scene, change_leaves_top_down)
{
    kuge::Engine engine;

    engine.scenes().change<Recorder>("A");
    engine.scenes().push<Recorder>("B");
    engine.scenes().push<Recorder>("C");
    engine.step(0.0);
    AssertEq(engine.scenes().size(), 3, "3 scenes stacked");
    g_events.clear();
    engine.scenes().change<Recorder>("D");
    engine.step(0.0);
    AssertStrEq(events().c_str(), "C:exit,C:dtor,B:exit,B:dtor,A:exit,A:dtor,D:ctor,D:enter",
        "from the top down, then the new one, got %s", events().c_str());
    AssertEq(engine.scenes().size(), 1, "only D is left");
}

Test(scene, push_pauses_pop_resumes)
{
    kuge::Engine engine;

    engine.scenes().change<Recorder>("A");
    engine.step(0.0);
    g_events.clear();
    engine.scenes().push<Recorder>("B");
    engine.step(0.0);
    AssertStrEq(events().c_str(), "A:pause,B:ctor,B:enter", "got %s", events().c_str());
    g_events.clear();
    engine.scenes().pop();
    engine.step(0.0);
    AssertStrEq(events().c_str(), "B:exit,B:dtor,A:resume", "got %s", events().c_str());
    AssertEq(engine.scenes().size(), 1, "A is back");
}

Test(scene, transitions_wait_for_the_end)
{
    kuge::Engine engine;

    engine.scenes().change<Changer>();
    g_events.clear();
    // 2 ticks in this loop: the scene asks for a change during the first one
    Assert(engine.step(2.0 / 60.0), "the engine keeps going");
    AssertStrEq(events().c_str(), "tick,asked,tick,changer:exit,next:ctor,next:enter",
        "the second tick still runs in the old scene, got %s", events().c_str());
}

Test(scene, systems_go_before_the_scene)
{
    kuge::Engine engine;

    engine.scenes().change<WithSystem>();
    engine.step(0.0);
    g_events.clear();
    engine.scenes().clear();
    AssertStrEq(events().c_str(), "scene:exit,system:dtor,scene:dtor",
        "onExit, then the systems, then the scene, got %s", events().c_str());
}

Test(scene, time_is_a_resource)
{
    kuge::Engine engine;
    Seen seen;

    engine.scenes().change<TimeScene>(std::ref(seen));
    engine.step(3.0 / 60.0);
    AssertEq(seen.ticks.size(), 3, "3 ticks");
    AssertEq(seen.ticks[0], 0, "the first tick is 0");
    AssertEq(seen.ticks[2], 2, "then 1, 2");
    AssertEq(seen.dts[1], 1.0 / 60.0, "the step never changes");
    AssertEq(seen.alphas.size(), 1, "one frame");
    engine.step(0.5 / 60.0);
    AssertEq(seen.ticks.size(), 3, "half a tick: no new tick");
    Assert(seen.alphas.back() > 0.49 && seen.alphas.back() < 0.51, "but the frame is halfway: %f", seen.alphas.back());
}

Test(scene, arguments_are_forwarded)
{
    kuge::Engine engine({.tickRate = 90});

    // One of them can only be moved
    engine.scenes().change<WithArgs>(42, std::string("hello"), std::make_unique<int>(7));
    engine.step(0.0);
    AssertEq(g_args.number, 42, "int");
    AssertStrEq(g_args.text.c_str(), "hello", "string");
    AssertEq(g_args.owned, 7, "a move-only argument");
    Assert(g_args.engine == &engine, "ctx().engine() is the engine");
    AssertEq(g_args.tickRate, 90, "ctx().time() is its clock");
}

Test(scene, pop_of_the_last_ends)
{
    kuge::Engine engine;

    engine.scenes().change<Recorder>("A");
    Assert(engine.step(0.0), "running");
    engine.scenes().pop();
    AssertEq(engine.step(0.0), false, "nothing left to run");
    Assert(engine.scenes().empty(), "empty");
}

Test(scene, pop_with_nothing_is_ok)
{
    kuge::Engine engine;

    engine.scenes().pop();
    AssertEq(engine.step(0.0), false, "no scene, nothing to run");
}

Test(scene, entered_scene_can_queue)
{
    // A scene asks for another one in onEnter(): both are done in the same loop
    class Splash : public kuge::Scene
    {
        public:
            void onEnter(void) override
            {
                note("splash:enter");
                ctx().scenes().change<Recorder>("menu");
            }
    };
    kuge::Engine engine;

    g_events.clear();
    engine.scenes().change<Splash>();
    engine.step(0.0);
    AssertStrEq(events().c_str(), "splash:enter,menu:ctor,menu:enter", "got %s", events().c_str());
}
