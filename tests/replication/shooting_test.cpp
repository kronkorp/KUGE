extern "C" {
    #include "kronklab/kronklab.h"
}
#include "PredictedSpawns.hpp"
#include "rep_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace kuge::replication;

    // The client makes its smoke at the click; the room makes the real one when the input arrives, `delayTicks` later
    struct Throw
    {
        RepSim                  sim;
        PredictedSpawns         spawns;
        kw::Entity              predicted = 0;
        bool                    started = false;

        explicit Throw(PredictedSpawns::Config config = {}) : spawns(sim.clientWorld, config) {}

        void click(PredictionKey key)
        {
            predicted = sim.clientWorld.create();
            sim.clientWorld.add<kuge::Transform2D>(predicted, kuge::Transform2D{{1.0f, 1.0f}});
            spawns.add(predicted, key);
        }

        // The room's real smoke
        void roomMakes(PredictionKey key)
        {
            const kw::Entity real = sim.spawn(BULLET, 1, 1);

            sim.serverWorld.add<PredictionKey>(real, key);
        }

        std::size_t smokes(void)
        {
            std::size_t count = 0;

            for ([[maybe_unused]] kw::Entity e : sim.clientWorld.view<PredictionKey>()) {
                ++count;
            }
            return count;
        }

        void run(int ticks)
        {
            for (int i = 0; i < ticks; ++i) {
                sim.step();
                spawns.update(sim.dt);
            }
        }
    };
}

Test(shooting, the_real_one_takes_over)
{
    Throw t;
    const PredictionKey key{7, 42, 0};

    t.run(3);
    t.click(key);
    AssertEq(t.smokes(), 1u, "the predicted entity is there on the tick of the click");
    t.run(4);
    t.roomMakes(key);          // the room gets the input a few ticks later
    // From the click to the real one, there is always exactly one smoke: no gap, no double
    int gaps = 0, doubles = 0;

    for (int i = 0; i < 20; ++i) {
        t.run(1);
        gaps += t.smokes() == 0 ? 1 : 0;
        doubles += t.smokes() > 1 ? 1 : 0;
    }
    AssertEq(gaps, 0, "never none");
    Assert(doubles <= 1, "a double at most for the one frame it takes to match (%d)", doubles);
    AssertEq(t.smokes(), 1u, "one in the end");
    AssertEq(t.spawns.pending(), 0u, "nothing waits any more");
    Assert(t.sim.clientCount() == 1, "and it is the room's, the replicated one");
}

Test(shooting, the_refused_one_goes)
{
    Throw t({.timeout = 0.5});
    bool told = false;

    t.spawns.onRefused([&](kw::Entity) { told = true; });
    t.click({7, 43, 0});
    t.run(20);
    AssertEq(t.smokes(), 1u, "before the delay it is still there");
    t.run(20);
    AssertEq(t.smokes(), 0u, "after 0.5 s with no word from the room it is removed");
    Assert(told, "and the game was told");
}

Test(shooting, a_key_is_one_thing)
{
    Throw t;
    int confirmed = 0;

    t.spawns.onConfirmed([&](kw::Entity predicted, kw::Entity real) {
        ++confirmed;
        Assert(predicted != real, "two entities");
        t.sim.clientWorld.remove(predicted);
    });
    const kw::Entity first = t.sim.clientWorld.create();
    const kw::Entity second = t.sim.clientWorld.create();

    t.spawns.add(first, {7, 50, 0});
    t.spawns.add(second, {7, 50, 1});   // the same input made two things
    t.roomMakes({7, 50, 1});
    t.run(10);
    AssertEq(confirmed, 1, "only the one with that key is confirmed");
    AssertEq(t.spawns.pending(), 1u, "the other still waits");
    t.spawns.forget(first);
    AssertEq(t.spawns.pending(), 0u, "forget lets go of it: the game removed it by itself");
    t.roomMakes({8, 50, 0});
    t.run(10);
    AssertEq(confirmed, 1, "a key of another player matches nothing");
}
