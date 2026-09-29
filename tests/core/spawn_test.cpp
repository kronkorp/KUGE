extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Engine.hpp"
#include "Ref.hpp"
#include "Stage.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.
// These tests run scenes on several threads: whatever they share is atomic or under a mutex.

namespace
{
    using Clock = std::chrono::steady_clock;

    bool waitUntil(const std::function<bool(void)>& done, double seconds = 30.0)
    {
        const auto end = Clock::now() + std::chrono::duration<double>(seconds);

        while (!done()) {
            if (Clock::now() > end) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    struct Tick : kw::ISystem
    {
        std::function<void(kw::World&)> fn;
        explicit Tick(std::function<void(kw::World&)> f) : fn(std::move(f)) {}
        bool handle(kw::World& world) override { fn(world); return true; }
    };

    class Actor;

    // What an Actor does. A scene is made from a copy: what is shared goes through pointers.
    struct Hooks
    {
        std::function<void(Actor&)>                        enter, exit, tick;
        std::function<void(Actor&, const kuge::Message&)>  message;
    };

    class Actor : public kuge::Scene
    {
        public:
            explicit Actor(Hooks hooks) : m_hooks(std::move(hooks)) {}

            using kuge::Scene::ctx;
            using kuge::Scene::world;

            void onEnter(void) override
            {
                if (m_hooks.tick) {
                    addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Tick>([this](kw::World&) { m_hooks.tick(*this); }));
                }
                if (m_hooks.enter) {
                    m_hooks.enter(*this);
                }
            }
            void onExit(void) override { if (m_hooks.exit) { m_hooks.exit(*this); } }
            void onMessage(const kuge::Message& message) override { if (m_hooks.message) { m_hooks.message(*this, message); } }

        private:
            Hooks m_hooks;
    };

    // Thread ids, written by the threads that run scenes
    struct Threads
    {
        std::mutex                 mutex;
        std::set<std::thread::id>  ids;

        void note(void)
        {
            std::lock_guard lock(mutex);
            ids.insert(std::this_thread::get_id());
        }
        std::size_t count(void)
        {
            std::lock_guard lock(mutex);
            return ids.size();
        }
        bool has(std::thread::id id)
        {
            std::lock_guard lock(mutex);
            return ids.count(id) != 0;
        }
    };

    struct Number { int value; };

    kuge::Engine::Config config(std::uint32_t tickRate, std::uint32_t workers = 0)
    {
        return kuge::Engine::Config{.tickRate = tickRate, .workers = workers};
    }
}

Test(spawn, dedicated_has_its_thread)
{
    Threads entered, ticked, exited;
    std::atomic<int> ticks{0};
    Hooks hooks;

    hooks.enter = [&entered](Actor&) { entered.note(); };
    hooks.tick = [&ticked, &ticks](Actor&) { ticked.note(); ++ticks; };
    hooks.exit = [&exited](Actor&) { exited.note(); };
    {
        kuge::Engine engine(config(200));
        const auto handle = engine.spawn<Actor>(kuge::RunPolicy::Dedicated, hooks);

        Assert(waitUntil([&ticks] { return ticks >= 10; }), "it ticks by itself, with no loop of ours");
        Assert(handle.alive() && engine.spawned() == 1, "and it is there");
        AssertEq(entered.count(), 1, "entered on one thread");
        Assert(!entered.has(std::this_thread::get_id()), "which is not this one");
        Assert(ticked.ids == entered.ids, "the ticks are on the same thread");
        AssertEq(exited.count(), 0, "not left yet");
    }
    AssertEq(exited.count(), 1, "the engine leaves it when it ends");
    Assert(exited.ids == entered.ids, "on the thread that ran it");
}

Test(spawn, dedicated_keeps_the_rate)
{
    std::atomic<int> ticks{0};
    Hooks hooks;

    hooks.tick = [&ticks](Actor&) { ++ticks; };
    kuge::Engine engine(config(100));
    const auto start = Clock::now();

    engine.spawn<Actor>(kuge::RunPolicy::Dedicated, hooks);
    Assert(waitUntil([&ticks] { return ticks >= 40; }), "40 ticks");
    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();

    // 40 ticks at 100 Hz take 0.4 s (a few can come at once to catch up a late loop)
    Assert(elapsed > 0.3, "not faster than its rate: %f s", elapsed);
}

Test(spawn, parent_and_messages)
{
    Threads parentThread;
    std::atomic<bool> hadParent{false};
    std::atomic<int> received{0};
    std::atomic<bool> lonelyOk{false};

    Hooks child;
    child.enter = [&hadParent](Actor& self) {
        hadParent = self.ctx().parent().valid();
        self.ctx().parent().send(Number{42});
    };
    Hooks parent;
    parent.enter = [child](Actor& self) { self.ctx().spawn<Actor>(kuge::RunPolicy::Dedicated, child); };
    parent.message = [&parentThread, &received](Actor&, const kuge::Message& message) {
        if (const auto* number = message.as<Number>()) {
            received = number->value;
            parentThread.note();
        }
    };
    Hooks lonely;
    lonely.enter = [&lonelyOk](Actor& self) { lonelyOk = !self.ctx().parent().valid(); };

    kuge::Engine engine(config(100));

    engine.scenes().change<Actor>(parent);
    engine.spawn<Actor>(kuge::RunPolicy::Dedicated, lonely);
    Assert(waitUntil([&] { engine.step(0.01); return received == 42; }), "the child's message arrives");
    Assert(hadParent, "the child knows its parent");
    Assert(parentThread.has(std::this_thread::get_id()), "and it is heard by the parent, on the parent's thread");
    Assert(waitUntil([&lonelyOk] { return static_cast<bool>(lonelyOk); }), "a scene spawned by the engine has no parent");
}

Test(spawn, main_policy_shares_the_loop)
{
    Threads mainTicks, sideTicks;
    std::atomic<int> main{0}, side{0};
    Hooks mainHooks, sideHooks;

    mainHooks.tick = [&](Actor&) { mainTicks.note(); ++main; };
    sideHooks.tick = [&](Actor&) { sideTicks.note(); ++side; };
    kuge::Engine engine(config(60));

    engine.scenes().change<Actor>(mainHooks);
    engine.spawn<Actor>(kuge::RunPolicy::Main, sideHooks);
    for (int i = 0; i < 30; ++i) {
        engine.step(1.0 / 60.0);
    }
    AssertEq(main.load(), 30, "the main scene ticked 30 times");
    AssertEq(side.load(), 30, "the spawned one ticked with it");
    Assert(mainTicks.ids == sideTicks.ids && mainTicks.has(std::this_thread::get_id()), "on this thread");
}

Test(spawn, main_policy_ends)
{
    std::atomic<int> exits{0};
    Hooks main, side;

    side.tick = [](Actor& self) { self.ctx().scenes().pop(); };
    side.exit = [&exits](Actor&) { ++exits; };
    kuge::Engine engine(config(60));

    engine.scenes().change<Actor>(main);
    engine.spawn<Actor>(kuge::RunPolicy::Main, side);
    for (int i = 0; i < 5; ++i) {
        engine.step(1.0 / 60.0);
    }
    AssertEq(exits.load(), 1, "left once, when it popped itself");
    AssertEq(engine.spawned(), 0, "and forgotten");
    Assert(engine.step(1.0 / 60.0), "the engine goes on");
}

// A spawned scene that throws is logged and stopped, on the main thread too: step() does not throw
Test(spawn, main_policy_that_throws)
{
    std::atomic<int> exits{0};
    std::atomic<int> mainTicks{0};
    Hooks main, bad;

    main.tick = [&](Actor&) { ++mainTicks; };
    bad.tick = [](Actor& self) {
        if (self.world().getResource<kuge::Time>().tick == 3) {
            throw std::runtime_error("boom");
        }
    };
    bad.exit = [&exits](Actor&) { ++exits; };
    kuge::Engine engine(config(60));

    engine.scenes().change<Actor>(main);
    engine.spawn<Actor>(kuge::RunPolicy::Main, bad);
    bool thrown = false;

    for (int i = 0; i < 10; ++i) {
        try {
            Assert(engine.step(1.0 / 60.0), "the engine goes on");
        } catch (...) {
            thrown = true;
        }
    }
    Assert(!thrown, "the exception does not come out of step()");
    AssertEq(exits.load(), 1, "the scene was left, once");
    AssertEq(engine.spawned(), 0, "and forgotten: it does not throw again");
    AssertEq(mainTicks.load(), 10, "the main scene was not disturbed");
}

// A scene does its ticks one after the other, on the workers, and never two at once
struct Counting
{
    static constexpr int      TICKS = 200;
    std::atomic<int>          counts[16] = {};
    std::atomic<int>          overlaps{0};
    std::atomic<int>          wrongTick{0};
    std::atomic<int>          exits{0};
    Threads                   threads;

    Hooks hooks(int index)
    {
        auto busy = std::make_shared<std::atomic<bool>>(false);
        Hooks h;

        h.tick = [this, index, busy](Actor& self) {
            if (busy->exchange(true)) {
                ++overlaps;
            }
            threads.note();
            const int done = counts[index];

            if (done < TICKS) {
                wrongTick += self.world().getResource<kuge::Time>().tick == static_cast<std::uint64_t>(done) ? 0 : 1;
                ++counts[index];
            } else {   // (a loop can still have ticks to run after a pop)
                self.ctx().scenes().pop();
            }
            *busy = false;
        };
        h.exit = [this](Actor&) { ++exits; };
        return h;
    }
};

Test(spawn, pooled_scenes)
{
    Counting counting;
    kuge::Engine engine(config(1000, 3));

    for (int i = 0; i < 16; ++i) {
        engine.spawn<Actor>(kuge::RunPolicy::Pooled, counting.hooks(i));
    }
    Assert(waitUntil([&counting] { return counting.exits == 16; }), "the 16 scenes end");
    for (int i = 0; i < 16; ++i) {
        AssertEq(counting.counts[i].load(), Counting::TICKS, "scene %d did all its ticks", i);
    }
    AssertEq(counting.overlaps.load(), 0, "never two ticks of a scene at once");
    AssertEq(counting.wrongTick.load(), 0, "the clock of each scene counts its own ticks");
    Assert(counting.threads.count() <= 3 && counting.threads.count() >= 1, "on the 3 workers, not 16 threads: %zu", counting.threads.count());
    Assert(!counting.threads.has(std::this_thread::get_id()), "and not on this thread");
    Assert(waitUntil([&engine] { return engine.spawned() == 0; }), "nothing runs any more");
}

Test(spawn, sixteen_dedicated_scenes)
{
    Counting counting;
    kuge::Engine engine(config(10000));

    for (int i = 0; i < 16; ++i) {
        engine.spawn<Actor>(kuge::RunPolicy::Dedicated, counting.hooks(i));
    }
    Assert(waitUntil([&counting] { return counting.exits == 16; }), "the 16 scenes end");
    for (int i = 0; i < 16; ++i) {
        AssertEq(counting.counts[i].load(), Counting::TICKS, "scene %d did all its ticks", i);
    }
    AssertEq(counting.overlaps.load(), 0, "never two ticks of a scene at once");
    AssertEq(counting.wrongTick.load(), 0, "each clock is its own");
    AssertEq(counting.threads.count(), 16, "one thread each");
}

Test(spawn, ten_thousand_ticks)
{
    constexpr int SCENES = 16;
    constexpr int TICKS  = 10000;
    std::atomic<int> counts[SCENES] = {};
    std::atomic<int> exits{0};
    std::atomic<int> wrong{0};
    kuge::Engine engine(config(20000));

    for (int i = 0; i < SCENES; ++i) {
        Hooks hooks;

        hooks.tick = [&counts, &wrong, i](Actor& self) {
            const int done = counts[i];

            if (done < TICKS) {
                wrong += self.world().getResource<kuge::Time>().tick == static_cast<std::uint64_t>(done) ? 0 : 1;
                ++counts[i];
            } else {
                self.ctx().scenes().pop();
            }
        };
        hooks.exit = [&exits](Actor&) { ++exits; };
        engine.spawn<Actor>(kuge::RunPolicy::Dedicated, hooks);
    }
    Assert(waitUntil([&exits] { return exits == SCENES; }, 120.0), "the scenes end");
    for (int i = 0; i < SCENES; ++i) {
        AssertEq(counts[i].load(), TICKS, "scene %d: every tick, none twice", i);
    }
    AssertEq(wrong.load(), 0, "the ticks are numbered without a gap");
}

// A lobby (on the main thread) makes rooms (on their own) one after another, and hears from each
struct RoomDone { int room; int ticks; };

Test(spawn, lobby_room_lobby)
{
    Threads roomThreads;
    std::vector<int> done;
    std::atomic<int> exits{0};
    Hooks lobby;

    auto makeRoom = [&roomThreads, &exits](int id) {
        Hooks room;
        auto ticks = std::make_shared<int>(0);

        room.tick = [&roomThreads, id, ticks](Actor& self) {
            roomThreads.note();
            if (++*ticks == 20) {
                self.ctx().parent().send(RoomDone{id, *ticks});
                self.ctx().scenes().pop();
            }
        };
        room.exit = [&exits](Actor&) { ++exits; };
        return room;
    };
    lobby.enter = [&](Actor& self) { self.ctx().spawn<Actor>(kuge::RunPolicy::Dedicated, makeRoom(1)); };
    lobby.message = [&](Actor& self, const kuge::Message& message) {
        if (const auto* result = message.as<RoomDone>()) {
            done.push_back(result->room);
            if (result->room < 3) {
                self.ctx().spawn<Actor>(kuge::RunPolicy::Dedicated, makeRoom(result->room + 1));
            }
        }
    };
    kuge::Engine engine(config(200));

    engine.scenes().change<Actor>(lobby);
    Assert(waitUntil([&] { engine.step(0.005); return done.size() == 3; }), "the three rooms report");
    AssertEq(done[0], 1, "in turn");
    AssertEq(done[1], 2, "in turn");
    AssertEq(done[2], 3, "in turn");
    AssertEq(exits.load(), 3, "each room was left");
    Assert(!roomThreads.has(std::this_thread::get_id()), "the rooms did not run on the lobby's thread");
    Assert(waitUntil([&] { engine.step(0.005); return engine.spawned() == 0; }), "and nothing runs once they are over");
}

Test(spawn, a_ring_of_scenes)
{
    constexpr int NODES = 8;
    constexpr int HOPS  = 600;
    struct Neighbour { kuge::SceneHandle next; };
    struct Token { int hop; };
    std::atomic<int> last{-1};
    std::atomic<int> outOfOrder{0};
    std::vector<kuge::SceneHandle> ring;
    kuge::Engine engine(config(1000));

    for (int i = 0; i < NODES; ++i) {
        Hooks node;
        auto next = std::make_shared<kuge::SceneHandle>();

        node.message = [&last, &outOfOrder, next, i](Actor&, const kuge::Message& message) {
            if (const auto* neighbour = message.as<Neighbour>()) {
                *next = neighbour->next;
            } else if (const auto* token = message.as<Token>()) {
                outOfOrder += token->hop % NODES == i ? 0 : 1;
                if (token->hop >= HOPS) {
                    last = token->hop;
                } else {
                    next->send(Token{token->hop + 1});
                }
            }
        };
        ring.push_back(engine.spawn<Actor>(kuge::RunPolicy::Dedicated, node));
    }
    for (int i = 0; i < NODES; ++i) {
        Assert(ring[static_cast<std::size_t>(i)].send(Neighbour{ring[static_cast<std::size_t>((i + 1) % NODES)]}), "the handle takes it");
    }
    ring[0].send(Token{0});
    Assert(waitUntil([&last] { return last == HOPS; }), "the token goes round the ring, from thread to thread");
    AssertEq(outOfOrder.load(), 0, "and always reaches the right scene");
}

Test(spawn, run_ends_the_spawned)
{
    std::atomic<int> ticks{0};
    std::atomic<int> exits{0};
    Hooks child;

    child.tick = [&ticks](Actor& self) {
        if (++ticks == 20) {
            self.ctx().engine().stop();   // from its own thread
        }
    };
    child.exit = [&exits](Actor&) { ++exits; };
    kuge::Engine engine(config(200));
    Hooks main;

    engine.scenes().change<Actor>(main);
    engine.spawn<Actor>(kuge::RunPolicy::Dedicated, child);
    engine.spawn<Actor>(kuge::RunPolicy::Pooled, child);
    AssertEq(engine.run(), 0, "run() ends when a scene, on another thread, stops the engine");
    AssertEq(exits.load(), 2, "the spawned scenes were left before it returned");
    AssertEq(engine.spawned(), 0, "and nothing runs");
}

Test(spawn, a_scene_that_throws)
{
    std::atomic<int> exits{0};
    Hooks bad;

    bad.tick = [](Actor& self) {
        if (self.world().getResource<kuge::Time>().tick == 3) {
            throw std::runtime_error("boom");
        }
    };
    bad.exit = [&exits](Actor&) { ++exits; };
    std::atomic<int> goodTicks{0};
    Hooks good;

    good.tick = [&goodTicks](Actor&) { ++goodTicks; };
    kuge::Engine engine(config(200));
    const auto a = engine.spawn<Actor>(kuge::RunPolicy::Dedicated, bad);
    const auto b = engine.spawn<Actor>(kuge::RunPolicy::Pooled, bad);
    const auto c = engine.spawn<Actor>(kuge::RunPolicy::Dedicated, good);

    Assert(waitUntil([&] { return !a.alive() && !b.alive(); }), "the scenes that threw are gone");
    AssertEq(exits.load(), 2, "and were left");
    const int before = goodTicks;

    Assert(waitUntil([&] { return goodTicks > before + 5; }), "the others go on");
    Assert(c.alive(), "still there");
}

Test(spawn, stop_from_a_handle)
{
    std::atomic<int> exits{0};
    Hooks hooks;

    hooks.exit = [&exits](Actor&) { ++exits; };
    kuge::Engine engine(config(200));
    const auto dedicated = engine.spawn<Actor>(kuge::RunPolicy::Dedicated, hooks);
    const auto pooled = engine.spawn<Actor>(kuge::RunPolicy::Pooled, hooks);

    Assert(dedicated.stop() && pooled.stop(), "asked");
    Assert(waitUntil([&exits] { return exits == 2; }), "both are left");
    Assert(waitUntil([&] { return !dedicated.alive() && !pooled.alive(); }), "and their handles are dead");
    Assert(waitUntil([&engine] { return engine.spawned() == 0; }), "nothing runs");
}

// Asking twice is asking once: the scene under the one that ends stays
Test(spawn, stop_twice_pops_once)
{
    std::atomic<int> exitsA{0}, exitsB{0};
    kuge::SceneHandle handleB;
    Hooks a, b;

    a.exit = [&exitsA](Actor&) { ++exitsA; };
    b.enter = [&handleB](Actor& self) { handleB = self.ctx().self(); };
    b.exit = [&exitsB](Actor&) { ++exitsB; };
    kuge::Engine engine(config(60));

    engine.scenes().change<Actor>(a);
    engine.scenes().push<Actor>(b);
    engine.step(1.0 / 60.0);
    Assert(handleB.alive(), "the second scene runs, over the first one");
    Assert(handleB.stop() && handleB.stop(), "asked twice");
    Assert(engine.step(1.0 / 60.0), "the engine goes on");
    AssertEq(exitsB.load(), 1, "the second scene was left");
    AssertEq(exitsA.load(), 0, "and the first one is still there");
    AssertEq(engine.scenes().size(), 1, "alone in the stack");
}

// The engine ends whatever is running, with no wait and no scene left behind
Test(spawn, a_hundred_stops)
{
    for (int round = 0; round < 100; ++round) {
        std::atomic<int> enters{0}, exits{0};
        Hooks hooks;

        hooks.enter = [&enters](Actor&) { ++enters; };
        hooks.tick = [](Actor&) {};
        hooks.exit = [&exits](Actor&) { ++exits; };
        const auto start = Clock::now();
        {
            kuge::Engine engine(config(200, 2));
            Hooks main;

            engine.scenes().change<Actor>(main);
            for (int i = 0; i < 3; ++i) {
                engine.spawn<Actor>(kuge::RunPolicy::Dedicated, hooks);
                engine.spawn<Actor>(kuge::RunPolicy::Pooled, hooks);
            }
            engine.spawn<Actor>(kuge::RunPolicy::Main, hooks);
            if (round % 2 == 0) {
                waitUntil([&] { engine.step(0.005); return enters == 7; });   // started, or not: both must end well
            }
        }
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();

        AssertEq(exits.load(), enters.load(), "round %d: everything that was entered was left", round);
        Assert(seconds < 5.0, "round %d ended in %f s", round, seconds);
    }
}

// What a module gives to the scenes of other threads: only what it says it can share
namespace
{
    struct MainOnlyResource { int value = 1; };
    struct SharedResource { int value = 2; };

    class MainOnlyModule : public kuge::Module
    {
        public:
            void inject(kw::World& world) override { world.addResource<MainOnlyResource>(); }
    };

    class SharedModule : public kuge::Module
    {
        public:
            bool sharedAcrossThreads(void) const noexcept override { return true; }
            void inject(kw::World& world) override { world.addResource<SharedResource>(); }
    };

    template<typename R>
    bool has(kw::World& world)
    {
        try {
            world.getResource<R>();
            return true;
        } catch (const kw::ResourceError&) {
            return false;
        }
    }
}

Test(spawn, modules_and_threads)
{
    std::atomic<int> mainSees{0}, otherSees{0};
    std::atomic<int> entered{0};
    kuge::Engine engine(config(100, 2));
    Hooks onMain, onOther;

    engine.addModule<MainOnlyModule>();
    engine.addModule<SharedModule>();
    onMain.enter = [&](Actor& self) {
        mainSees += (has<MainOnlyResource>(self.world()) ? 1 : 0) + (has<SharedResource>(self.world()) ? 10 : 0);
        ++entered;
    };
    onOther.enter = [&](Actor& self) {
        otherSees += (has<MainOnlyResource>(self.world()) ? 1 : 0) + (has<SharedResource>(self.world()) ? 10 : 0);
        ++entered;
    };
    engine.scenes().change<Actor>(onMain);
    engine.spawn<Actor>(kuge::RunPolicy::Dedicated, onOther);
    engine.spawn<Actor>(kuge::RunPolicy::Pooled, onOther);
    engine.step(0.0);
    Assert(waitUntil([&entered] { return entered == 3; }), "all three are entered");
    AssertEq(mainSees.load(), 11, "the main scene gets everything");
    AssertEq(otherSees.load(), 20, "the scenes of other threads only get what is shared (twice: 10 + 10)");
}

// A system that throws: the exception comes out of the loop, the systems after it are skipped
// for that pass, and nothing is left behind (ASan / LeakSanitizer check the last part)
namespace
{
    class Thrower : public kuge::Scene
    {
        public:
            Thrower(int* first, int* second) : m_first(first), m_second(second) {}

            void onEnter(void) override
            {
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Tick>([this](kw::World&) {
                    if (++*m_first == 3) {
                        throw std::runtime_error("boom");
                    }
                }));
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Tick>([this](kw::World&) { ++*m_second; }));
            }

        private:
            int* m_first;
            int* m_second;
    };
}

Test(scene, a_system_that_throws)
{
    int first = 0, second = 0;
    std::string what;
    kuge::Engine engine(config(60));

    engine.scenes().change<Thrower>(&first, &second);
    for (int i = 0; i < 5 && what.empty(); ++i) {
        try {
            engine.step(1.0 / 60.0);
        } catch (const std::runtime_error& error) {
            what = error.what();
        }
    }
    AssertStrEq(what.c_str(), "boom", "the exception comes out of the loop");
    AssertEq(first, 3, "on the third tick");
    AssertEq(second, 2, "and the system after it did not run in that pass");
    engine.step(1.0 / 60.0);
    AssertEq(second, 3, "the scene can go on if the game wants to");
}
