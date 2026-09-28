# Step 7: Threads

A game can have scenes that run next to the main ones: a lobby and its rooms on a server, the room of a player who
hosts, a HUD. They are **spawned**. This step follows `spawn` to the thread that runs the scene, shows how two scenes
talk, and how everything stops.

## One loop per spawned scene

```cpp
// modules/core/src/Engine.cpp, Engine::spawnScene
const SceneHandle handle = scene->handle();
...
auto loop = std::make_unique<SceneLoop>(*this, m_config.tickRate, m_config.maxCatchUp, std::move(parent),
    policy == RunPolicy::Main);

loop->scenes().changeTo(std::move(scene));          // queued: entered by the thread that runs the loop
reap();
std::lock_guard lock(m_spawnMutex);
switch (policy) {
    case RunPolicy::Main:
        m_sideAdded.push_back(std::move(loop));     // stepped by the main thread, from the next loop on
        break;
    case RunPolicy::Dedicated: {
        auto runner = std::make_unique<Dedicated>();
        Dedicated& ref = *runner;

        runner->loop = std::move(loop);
        m_dedicated.push_back(std::move(runner));
        ref.thread = std::thread([this, &ref] { runDedicated(ref); });
        break;
    }
    case RunPolicy::Pooled:
        driver().add(std::move(loop));              // the TickDriver hands its ticks to the workers
        break;
}
return handle;
```

A spawned scene gets a `SceneLoop` of its own (step 2): its own accumulator, its own `Time`, its own stack. The scene
is built by the caller, with the arguments, but it is **entered** by the thread that will run it: its first
`ready()` does the `changeTo`. So `onEnter`, the ticks, and `onExit` all happen on the same thread. The spawning
scene's handle becomes the new scene's `ctx().parent()`.

## Three ways to run a loop

**`Main`**: on the main thread, after the main scene's loop, in every `Engine::step`:

```cpp
// modules/core/src/Engine.cpp, Engine::stepSideLoops
for (std::size_t i = 0; i < m_side.size();) {
    SceneLoop& loop = *m_side[i];

    if (loop.step(frameSeconds, m_stop)) {
        ++i;
    } else {
        loop.finish();                               // no scene left in it: forget it
        m_side.erase(m_side.begin() + static_cast<std::ptrdiff_t>(i));
    }
}
```

(A known limit: an exception thrown by a `Main` scene is not caught here, so it comes out of `step()`. Dedicated and
pooled scenes catch it, as shown below.)

**`Dedicated`**: a thread of its own, running the same loop as `run()`:

```cpp
// modules/core/src/Engine.cpp, Engine::runDedicated
try {
    while (true) {
        ... frame = time since the last loop
        if (!runner.loop->step(frame, m_stopSpawned)) {
            break;                                   // no scene left, or the engine stops
        }
        if (m_wake.waitFor(runner.loop->untilNextTick())) {
            break;                                   // a sleep that "stop" interrupts at once
        }
    }
} catch (const std::exception& e) {
    Logger::logger().error("A scene threw and is stopped: {}", e.what());
}
runner.loop->finish();                               // left by the thread that ran it
runner.finished = true;                              // reap() will join the thread
```

`m_wake` is a `StopSignal`: a flag with a condition variable. `waitFor` is a `wait_for` on it. A dedicated thread
sleeps until its next tick, but wakes up at once when the engine stops, so stopping a server with twenty rooms
does not wait for twenty sleeps.

**`Pooled`**: many loops on a few workers. A `TickDriver` owns one thread that only decides *who is due*, and gives
the ticks to kronkpool's workers:

```cpp
// modules/core/src/TickDriver.cpp, TickDriver::driverMain (under m_mutex)
for (auto& item : m_items) {
    if (item->running || item->finished) {
        continue;                                    // never two ticks of one loop at once
    }
    if (item->stopping || item->due <= now) {
        item->running = true;
        m_pool.post([this, raw] { tick(*raw); });    // a worker runs the loop's step
    } else {
        wakeAt = std::min(wakeAt, item->due);        // sleep until the next one is due
    }
}
m_cond.wait_until(lock, wakeAt);

// TickDriver::tick, on a worker, when the step is done
item.running = false;
item.due = Clock::now() + item.loop->untilNextTick();
m_cond.notify_all();                                 // the driver thinks again
```

A late loop does not queue work: it simply was not given a new tick while its last one ran, and at its next step its
own accumulator owes it the missed ticks (at most `maxCatchUp`, step 2).

## Messages

```cpp
// modules/core/src/Message.hpp
namespace detail
{
    template<typename T>
    inline constexpr char messageTag = 0;            // one address per type
}

class Message
{
    public:
        template<typename T>
        explicit Message(T&& value)
            : m_holder(std::make_unique<Holder<std::decay_t<T>>>(std::forward<T>(value))),
              m_tag(&detail::messageTag<std::decay_t<T>>)
        {
        }

        template<typename T>
        const T* as(void) const noexcept
        {
            return is<T>() ? &static_cast<const Holder<T>&>(*m_holder).value : nullptr;
        }
        ...
    private:
        std::unique_ptr<Base> m_holder;              // the value, moved in: it may be a type that cannot be copied
        const char*           m_tag;
};
```

A message is any value, type-erased. The type is recognised by the **address** of a variable that exists once per
type (`messageTag<T>`), so `as<T>()` is one pointer comparison, with no RTTI. The value is moved into the message,
the message into the receiver's mailbox (step 5), and only the receiver's thread ever touches it again. Nothing is
shared: the sender gave it away.

A `SceneHandle` is a `shared_ptr<Mailbox>`, and posting takes the mailbox's mutex. That mutex is the only point
where two scenes' threads meet. The receiver drains the whole mailbox at the start of its next loop (step 5): a
message costs **one loop** of the receiver.

## Stopping

```cpp
// modules/core/src/Engine.cpp, Engine::stopSpawned (called by run() when it ends, and by ~Engine)
m_stopSpawned = true;                                // loops see it at their next step
m_wake.request();                                    // dedicated threads wake up now
{
    std::lock_guard lock(m_spawnMutex);
    dedicated = std::move(m_dedicated);              // taken out under the lock, joined without it
    side = std::move(m_side);
    ... m_sideAdded, m_driver
}
for (auto& runner : dedicated) {
    runner->thread.join();                           // each leaves its scenes on its own thread
}
driver.reset();                                      // stopAll: a last tick on the pool leaves the pooled scenes
for (auto& loop : side) {
    loop->finish();
}
```

The threads are joined **without** holding the lock. A scene that is ending may itself try to spawn (a lobby opening
a room at the last moment): it must be able to take the lock, see `m_stopSpawned`, and give up, instead of
deadlocking against the thread that joins it.

## Lab

`example/lab/lab07_threads.cpp`:

```cpp
// Lab 7: two scenes that only talk through messages. First next to each other on the main thread
// (so that the output is always the same), then with one on a thread of its own.
#include "Engine.hpp"
#include "Logger.hpp"
#include "Scene.hpp"
#include "Stage.hpp"
#include <cstdio>
#include <functional>
#include <thread>

namespace
{
    struct Ping { int n; };
    struct Pong { int n; bool otherThread; };

    std::thread::id g_mainThread;

    class Say final : public kw::ISystem
    {
        public:
            explicit Say(std::function<void(kw::World&)> fn) : m_fn(std::move(fn)) {}
            bool handle(kw::World& world) override { m_fn(world); return true; }

        private:
            std::function<void(kw::World&)> m_fn;
    };

    // Answers each Ping to the scene that spawned it
    class Echo final : public kuge::Scene
    {
        public:
            void onMessage(const kuge::Message& message) override
            {
                if (const auto* ping = message.as<Ping>()) {
                    ctx().parent().send(Pong{ping->n, std::this_thread::get_id() != g_mainThread});
                }
            }
    };

    class Main final : public kuge::Scene
    {
        public:
            Main(kuge::RunPolicy policy, bool verbose) : m_policy(policy), m_verbose(verbose) {}

            void onEnter(void) override
            {
                m_echo = ctx().spawn<Echo>(m_policy);          // this scene becomes its parent
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Say>([this](kw::World& w) {
                    if (m_sent < 3) {
                        m_echo.send(Ping{++m_sent});
                        if (m_verbose) {
                            std::printf("  tick %llu: ping %d sent\n", static_cast<unsigned long long>(w.getResource<kuge::Time>().tick), m_sent);
                        }
                    }
                }));
            }

            void onMessage(const kuge::Message& message) override
            {
                if (const auto* pong = message.as<Pong>()) {
                    ++m_received;
                    m_allFromOtherThread = m_allFromOtherThread && pong->otherThread;
                    if (m_verbose) {
                        std::printf("  before tick %llu: pong %d is back\n", static_cast<unsigned long long>(ctx().time().tick), pong->n);
                    }
                    if (m_received == 3) {
                        std::printf("  3 pongs back; all answered from another thread: %s\n", m_allFromOtherThread ? "yes" : "no");
                        ctx().engine().stop();
                    }
                }
            }

        private:
            kuge::RunPolicy   m_policy;
            bool              m_verbose;
            kuge::SceneHandle m_echo;
            int               m_sent = 0;
            int               m_received = 0;
            bool              m_allFromOtherThread = true;
    };
}

int main()
{
    Logger::logger().enable(false);
    g_mainThread = std::this_thread::get_id();
    {
        std::printf("Echo spawned with RunPolicy::Main, the loop driven by hand:\n");
        kuge::Engine engine({.mode = kuge::Engine::Mode::Headless, .tickRate = 60});

        engine.scenes().change<Main>(kuge::RunPolicy::Main, true);
        for (int i = 0; i < 6 && engine.step(1.0 / 60.0); ++i) {
        }
    }
    {
        std::printf("Echo spawned with RunPolicy::Dedicated, engine.run() on the real clock:\n");
        kuge::Engine engine({.mode = kuge::Engine::Mode::Headless, .tickRate = 60});

        engine.scenes().change<Main>(kuge::RunPolicy::Dedicated, false);
        engine.run();
    }
}
```

What it prints (`./build/example/lab07_threads`):

```text
Echo spawned with RunPolicy::Main, the loop driven by hand:
  tick 0: ping 1 sent
  before tick 1: pong 1 is back
  tick 1: ping 2 sent
  before tick 2: pong 2 is back
  tick 2: ping 3 sent
  before tick 3: pong 3 is back
  3 pongs back; all answered from another thread: no
Echo spawned with RunPolicy::Dedicated, engine.run() on the real clock:
  3 pongs back; all answered from another thread: yes
```

## Reading the output

- With `RunPolicy::Main`, everything is on one thread and the output is always the same. The ping sent during
  tick 0 reaches Echo in the same loop: Echo is stepped after the main scene (`stepSideLoops`), drains its mailbox,
  and answers. The pong waits in Main's mailbox, and Main gets it at the start of its **next** loop, "before tick 1".
  The whole round trip costs one loop, because Echo happens to run after Main in the same loop.
- With `RunPolicy::Dedicated`, the same code runs with Echo on its own thread, and the answers indeed come from
  another thread. Only the summary is printed because the exact ticks now depend on how the two threads' clocks fall.
- `ctx().parent()` in Echo is Main, which spawned it. Neither scene can see the other's World: all they share is two
  mailboxes.

Next: [Step 8: The client](../08-the-client/README.md).
