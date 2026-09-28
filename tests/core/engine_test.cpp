extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Engine.hpp"
#include "Stage.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <chrono>
#include <csignal>
#include <memory>
#include <stdexcept>
#include <thread>

// NOTE: kronklab test names are limited to 31 characters.

Test(engine, construct_destroy)
{
    auto engine = std::make_unique<kuge::Engine>();

    AssertNotNull(engine.get(), "the engine must be created");
    engine.reset();
}

// kuge-core carries kronkworld (and through it kronkflow and kronkpool): a
// World must be usable from the module alone, with the schedules of the libs.
struct Ticks { size_t fixed = 0; size_t frame = 0; };

template<bool IsFixed>
class Tick : public kw::ISystem
{
    public:
        bool handle(kw::World& world) override
        {
            auto& ticks = world.getResource<Ticks>();

            ++(IsFixed ? ticks.fixed : ticks.frame);
            return true;
        }
};

Test(engine, world_has_schedules)
{
    kw::World world;

    world.addResource<Ticks>();
    world.addSystem(kw::Schedule::Fixed, 0, std::make_unique<Tick<true>>());
    world.addSystem(kw::Schedule::Frame, 0, std::make_unique<Tick<false>>());
    world.runOnce(kw::Schedule::Fixed);
    world.runOnce(kw::Schedule::Fixed);
    world.runOnce(kw::Schedule::Frame);
    AssertEq(world.getResource<Ticks>().fixed, 2, "2 fixed ticks, got %zu", world.getResource<Ticks>().fixed);
    AssertEq(world.getResource<Ticks>().frame, 1, "1 frame tick, got %zu", world.getResource<Ticks>().frame);
}

namespace
{
    struct Counts {
        std::uint64_t fixed = 0;
        std::uint64_t frame = 0;
        std::uint64_t leftAt = 0;
        bool          left = false;
    };

    // Counts what runs; can also stop, pop or signal the engine at a given tick
    class Loop : public kw::ISystem
    {
        public:
            enum class Then { Nothing, Stop, Pop, Signal };

            Loop(Counts& counts, kuge::Engine& engine, bool fixed, std::uint64_t at, Then then)
                : m_counts(counts), m_engine(engine), m_fixed(fixed), m_at(at), m_then(then) {}

            bool handle(kw::World&) override
            {
                if (!m_fixed) {
                    ++m_counts.frame;
                    return true;
                }
                if (++m_counts.fixed == m_at) {
                    switch (m_then) {
                        case Then::Stop:   m_engine.stop(); break;
                        case Then::Pop:    m_engine.scenes().pop(); break;
                        case Then::Signal: std::raise(SIGINT); break;
                        case Then::Nothing: break;
                    }
                }
                return true;
            }

        private:
            Counts&        m_counts;
            kuge::Engine&  m_engine;
            bool           m_fixed;
            std::uint64_t  m_at;
            Then           m_then;
    };

    class Counting : public kuge::Scene
    {
        public:
            Counting(Counts& counts, Loop::Then then = Loop::Then::Nothing, std::uint64_t at = 0)
                : m_counts(counts), m_then(then), m_at(at) {}

            void onEnter(void) override
            {
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation,
                    std::make_unique<Loop>(m_counts, ctx().engine(), true, m_at, m_then));
                addSystem(kw::Schedule::Frame, kuge::stage::Render,
                    std::make_unique<Loop>(m_counts, ctx().engine(), false, 0, Loop::Then::Nothing));
            }

            void onExit(void) override { m_counts.left = true; m_counts.leftAt = m_counts.fixed; }

        private:
            Counts&       m_counts;
            Loop::Then    m_then;
            std::uint64_t m_at;
    };
}

Test(engine, ticks_then_one_frame)
{
    kuge::Engine engine({.tickRate = 60});
    Counts counts;

    engine.scenes().change<Counting>(std::ref(counts));
    engine.step(3.0 / 60.0);
    AssertEq(counts.fixed, 3, "3 ticks, got %lu", (unsigned long)counts.fixed);
    AssertEq(counts.frame, 1, "then 1 frame, got %lu", (unsigned long)counts.frame);
    engine.step(0.5 / 60.0);
    AssertEq(counts.fixed, 3, "half a tick is not enough");
    AssertEq(counts.frame, 2, "but the frame is drawn");
    engine.step(0.5 / 60.0);
    AssertEq(counts.fixed, 4, "the two halves make a tick");
    AssertEq(counts.frame, 3, "and a frame");
}

Test(engine, catch_up_is_limited)
{
    kuge::Engine engine({.tickRate = 60, .maxCatchUp = 3});
    Counts counts;

    engine.scenes().change<Counting>(std::ref(counts));
    engine.step(10.0);
    AssertEq(counts.fixed, 3, "3 ticks at most, got %lu", (unsigned long)counts.fixed);
    engine.step(0.0);
    AssertEq(counts.fixed, 3, "the rest is not owed");
}

Test(engine, invalid_config_throws)
{
    bool rate = false;
    bool cap = false;

    try { kuge::Engine engine({.tickRate = 0}); } catch (const std::invalid_argument&) { rate = true; }
    try { kuge::Engine engine({.maxCatchUp = 0}); } catch (const std::invalid_argument&) { cap = true; }
    Assert(rate, "tickRate 0");
    Assert(cap, "maxCatchUp 0");
}

Test(engine, run_ends_with_no_scene)
{
    kuge::Engine engine;

    AssertEq(engine.run(), 0, "nothing to run: it returns");
}

Test(engine, run_ends_on_stop)
{
    kuge::Engine engine({.tickRate = 240});
    Counts counts;

    engine.scenes().change<Counting>(std::ref(counts), Loop::Then::Stop, 5);
    AssertEq(engine.run(), 0, "returns 0");
    AssertEq(counts.fixed, 5, "not a tick after stop(), got %lu", (unsigned long)counts.fixed);
    Assert(counts.left, "the scene was left");
    Assert(engine.scenes().empty(), "nothing stays");
}

Test(engine, run_ends_on_last_pop)
{
    kuge::Engine engine({.tickRate = 240});
    Counts counts;

    engine.scenes().change<Counting>(std::ref(counts), Loop::Then::Pop, 3);
    AssertEq(engine.run(), 0, "returns 0");
    AssertEq(counts.fixed, 3, "the scene ran until it popped itself");
    Assert(counts.left, "and was left");
}

Test(engine, stop_from_another_thread)
{
    kuge::Engine engine({.tickRate = 240});
    Counts counts;
    std::thread stopper([&engine] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        engine.stop();
    });

    engine.scenes().change<Counting>(std::ref(counts));
    engine.run();
    stopper.join();
    Assert(counts.fixed > 0, "it ran meanwhile");
    Assert(counts.left, "and left its scene");
}

Test(engine, stop_before_run_is_kept)
{
    kuge::Engine engine({.tickRate = 240});
    Counts counts;

    engine.stop();
    engine.scenes().change<Counting>(std::ref(counts));
    engine.run();
    AssertEq(counts.fixed, 0, "a stop() that came first is not lost");
}

Test(engine, engine_can_run_again)
{
    kuge::Engine engine({.tickRate = 240});
    Counts first;
    Counts second;

    engine.scenes().change<Counting>(std::ref(first), Loop::Then::Stop, 2);
    engine.run();
    engine.scenes().change<Counting>(std::ref(second), Loop::Then::Stop, 2);
    engine.run();
    AssertEq(first.fixed, 2, "first run");
    AssertEq(second.fixed, 2, "second run: stop() does not stay on");
}

Test(engine, sigint_ends_run)
{
    struct sigaction before = {};
    struct sigaction after = {};
    kuge::Engine engine({.tickRate = 240});
    Counts counts;

    sigaction(SIGINT, nullptr, &before);
    engine.scenes().change<Counting>(std::ref(counts), Loop::Then::Signal, 3);
    AssertEq(engine.run(), 0, "the signal ends run() instead of the process");
    Assert(counts.fixed >= 3 && counts.fixed < 100, "it stopped soon after, got %lu", (unsigned long)counts.fixed);
    Assert(counts.left, "and the scene was left");
    sigaction(SIGINT, nullptr, &after);
    Assert(before.sa_handler == after.sa_handler, "the previous handler is back");
}
