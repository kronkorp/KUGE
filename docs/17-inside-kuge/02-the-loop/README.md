# Step 2: The loop

`engine.run()` is a `while` loop around `engine.step(seconds)`. Everything else in KUGE happens inside one call
of `step`, so this is the first thing to know by heart.

## `run()`: a clock, a step, a sleep

```cpp
// modules/core/src/Engine.cpp, Engine::run
SignalGuard signals;                                   // SIGINT and SIGTERM now only set a flag
auto last = Clock::now();

while (true) {
    const auto now = Clock::now();
    const double frame = std::chrono::duration<double>(now - last).count();

    last = now;
    if (g_signalled) {
        stop();
    }
    if (!step(frame)) {
        break;
    }
    // Nothing to do until the next tick (or the next frame, if they are more frequent)
    const double wait = m_config.maxFps == 0
        ? m_main.untilNextTick()
        : 1.0 / m_config.maxFps - std::chrono::duration<double>(Clock::now() - now).count();

    if (wait > 0.0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(wait));
    }
}
stopSpawned();                                         // every spawned scene is left (step 7)
m_main.finish();                                       // then the main ones
m_stop = false;
```

- The duration of a frame is measured with `steady_clock`, and **handed to `step`**. `step` never reads a clock. That
  is the whole trick behind the tests that play ten seconds of a game in a millisecond: they call `step(1.0 / 60)`
  themselves.
- Ctrl+C does not kill the process. `SignalGuard` installs a handler that only sets `g_signalled`, and the loop turns
  that into `stop()`, so every scene gets its `onExit()`.
- With `maxFps == 0` (the default), the loop sleeps exactly until the next tick is due: one loop per tick. With
  `maxFps` set, it runs that many loops per second, whether a tick is due or not.

## `step()`: one loop

```cpp
// modules/core/src/Engine.cpp, Engine::step
if (!m_main.ready() || m_stop) {                 // 1. do the scene changes that wait; is there a scene?
    return false;
}
for (auto& module : m_modules) {                 // 2. modules: the client reads the window and the keys here
    module->beginFrame(*this);
}
if (m_stop) {
    return false;                                //    (the window was closed during beginFrame)
}
m_main.run(frameSeconds, m_stop);                // 3. the top main scene: messages, ticks, one frame
stepSideLoops(frameSeconds);                     // 4. scenes spawned with RunPolicy::Main (step 7)
for (auto& module : m_modules) {                 // 5. modules: the client shows the picture here
    module->endFrame(*this);
}
m_main.settle();                                 // 6. the scene changes asked during this loop
reap();                                          // 7. join the threads of spawned scenes that ended
return !m_stop && !m_main.scenes().empty();
```

Step 3 is where the game runs:

```cpp
// modules/core/src/SceneLoop.cpp, SceneLoop::run
Scene* scene = m_scenes.top();                   // only the top of the stack runs

scene->deliverMessages();                        // what other scenes sent since the last loop (step 5)
const std::uint32_t ticks = m_timestep.advance(frameSeconds);

for (std::uint32_t i = 0; i < ticks && !stop; ++i) {
    m_time.tick = m_ticksRun;
    scene->fixedTick(m_time);                    // the Fixed schedule of its World
    ++m_ticksRun;
}
m_time.tick = m_ticksRun;
m_time.alpha = m_timestep.alpha();
m_time.frameDt = frameSeconds;
scene->frame(m_time);                            // the Frame schedule: once, whatever the number of ticks
```

`fixedTick` and `frame` copy `m_time` into the World's `Time` resource, then run one schedule of the World (step 4).
That copy is how a system reads `world.getResource<kuge::Time>()`.

## The accumulator

How many ticks does a frame of `frameSeconds` owe? `FixedTimestep` keeps what was not turned into ticks yet, and
keeps it **in ticks, not in seconds**:

```cpp
// modules/core/src/FixedTimestep.cpp, FixedTimestep::advance
if (!(frameSeconds > 0.0) || !std::isfinite(frameSeconds)) {
    return 0;                                     // a negative, NaN or infinite frame counts for nothing
}
m_ticks += frameSeconds * m_tickRate;             // e.g. 0.010 s at 60 Hz: 0.6 of a tick
whole = std::min(std::floor(m_ticks + EPSILON), MAX_TICKS);
m_ticks = std::max(m_ticks - whole, 0.0);         // what is left is less than a tick: that is alpha
due = static_cast<std::uint64_t>(whole);
run = std::min<std::uint64_t>(due, m_maxCatchUp);
m_dropped += due - run;                           // owed, but never run
return static_cast<std::uint32_t>(run);
```

- **`EPSILON` (1e-9)**: 1/60 has no exact binary value. Without it, sixty frames of 1/60 s could add up to
  59.999999… ticks and give 59 ticks.
- **`maxCatchUp` (5 by default)**: after a long frame (a breakpoint, the window being dragged), the engine owes many
  ticks. Running them all would make the next frame long too, and the one after longer: the "spiral of death". It
  runs at most 5, and **forgets** the rest (`dropped()` counts them). The game slows down for a moment instead of
  freezing.
- **`alpha()`** is the part of a tick that is left over, between 0 and 1. A frame uses it to draw things between
  their last two positions (step 8).
- **`untilNextTick()`** is `(1 - m_ticks) / tickRate`: the time to sleep until the next tick is due. That is what
  `run()` sleeps.

## Lab

`example/lab/lab02_loop.cpp`:

```cpp
// Lab 2: one loop of the engine, traced. A module and two systems say when they are called,
// and the loop is driven by hand with frames of made-up durations.
#include "Engine.hpp"
#include "Logger.hpp"
#include "Scene.hpp"
#include "Stage.hpp"
#include <cstdio>
#include <functional>

namespace
{
    // A system made of a function
    class Say final : public kw::ISystem
    {
        public:
            explicit Say(std::function<void(kw::World&)> fn) : m_fn(std::move(fn)) {}
            bool handle(kw::World& world) override { m_fn(world); return true; }

        private:
            std::function<void(kw::World&)> m_fn;
    };

    class Tracer final : public kuge::Module
    {
        public:
            void inject(kw::World&) override { std::puts("  module  inject (a scene is being entered)"); }
            void beginFrame(kuge::Engine&) override { std::puts("  module  beginFrame"); }
            void endFrame(kuge::Engine&) override { std::puts("  module  endFrame"); }
    };

    class Traced final : public kuge::Scene
    {
        public:
            void onEnter(void) override
            {
                std::puts("  scene   onEnter");
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Say>([](kw::World& w) {
                    std::printf("  Fixed   tick %llu\n", static_cast<unsigned long long>(w.getResource<kuge::Time>().tick));
                }));
                addSystem(kw::Schedule::Frame, kuge::stage::Render, std::make_unique<Say>([](kw::World& w) {
                    std::printf("  Frame   alpha %.2f\n", w.getResource<kuge::Time>().alpha);
                }));
            }
    };
}

int main()
{
    Logger::logger().enable(false);    // (the engine's own log lines would get in the way)
    kuge::Engine engine({.mode = kuge::Engine::Mode::Headless, .tickRate = 60, .maxCatchUp = 5});

    engine.addModule<Tracer>();
    engine.scenes().change<Traced>();  // asked now, done at the start of the next loop
    for (const double frame : {0.010, 0.010, 0.030, 0.200}) {
        std::printf("step(%.3f s)\n", frame);
        engine.step(frame);
    }
}
```

What it prints (`./build/example/lab02_loop`):

```text
step(0.010 s)
  module  inject (a scene is being entered)
  scene   onEnter
  module  beginFrame
  Frame   alpha 0.60
  module  endFrame
step(0.010 s)
  module  beginFrame
  Fixed   tick 0
  Frame   alpha 0.20
  module  endFrame
step(0.030 s)
  module  beginFrame
  Fixed   tick 1
  Fixed   tick 2
  Frame   alpha 0.00
  module  endFrame
step(0.200 s)
  module  beginFrame
  Fixed   tick 3
  Fixed   tick 4
  Fixed   tick 5
  Fixed   tick 6
  Fixed   tick 7
  Frame   alpha 0.00
  module  endFrame
```

## Reading the output

- **The first loop** starts by doing the pending `change<Traced>()`: the module's `inject` runs, then `onEnter`,
  and only then `beginFrame`. A scene is entered at the start of a loop, before anything else happens in it.
- **0.010 s** is 0.6 of a tick: no tick runs, but the frame does (`alpha 0.60`). The Frame schedule runs once per
  loop, even when no tick does.
- **Another 0.010 s** brings the total to 1.2 ticks: tick 0 runs, and 0.2 is left (`alpha 0.20`).
- **0.030 s** is 1.8 more: 2.0 in all, so ticks 1 and 2 run, and nothing is left (`alpha 0.00`).
- **0.200 s** owes 12 ticks. Only 5 run (ticks 3 to 7): `maxCatchUp`. The other 7 are dropped, so that game time is
  lost for good.
- In every loop the order is the same: `beginFrame`, the ticks, the frame, `endFrame`.

Try `.maxCatchUp = 20` and run it again: the last loop runs twelve ticks.

Next: [Step 3: The ECS](../03-the-ecs/README.md).
