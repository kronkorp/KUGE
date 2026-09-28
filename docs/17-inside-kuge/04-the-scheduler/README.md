# Step 4: The scheduler

A system is a class with one method, `bool handle(kw::World&)`. This step follows what happens between
`addSystem(...)` in a scene and the moment `handle` is called. There are three layers: KUGE's `Scene`, kronkworld's
`SystemManager`, and kronkflow's `kfScheduler`, a task scheduler written in C.

## Two schedulers per World

```cpp
// kronkworld/system/System.hpp, SystemManager
Scheduler m_fixed;     // runOnce(Schedule::Fixed): once per tick
Scheduler m_frame;     // runOnce(Schedule::Frame): once per frame
```

Each `kw::World` has two kronkflow schedulers. `Scene::fixedTick` runs one step of the first,
`Scene::frame` one step of the second (step 2). Each scheduler counts its own steps: a `delay` or an `interval`
counts ticks for a Fixed system, and frames for a Frame system.

## A system becomes a task

```cpp
// kronkworld/system/System.hpp, SystemManager::addSystem
ISystem* rawSystem = system.release();
auto id = scheduler(schedule).pushTask((kfTaskOpt){
    [](void *ctx, void *arg) -> int {                     // what the C scheduler calls
        auto task = static_cast<ISystem *>(arg);
        auto ret = task->handle(*static_cast<World *>(ctx));
        task->markAsDone();
        return ret;
    },
    static_cast<void *>(rawSystem),                       // the system itself, as the task's data
    [](void *thing){ delete static_cast<ISystem *>(thing); },   // how to free it
    stage,
    (kfRWMasks){mask.read_mask, mask.write_mask}},
    delay, interval);
```

The C++ object goes to C as a `void*`, with two function pointers: one that calls it, and one that deletes it.
`kfScheduler_addTask` gives the task a new id, sets its **target tick** to `current tick + delay`, and puts it in a
min-heap ordered by `(target, id)`.

## One step of the scheduler

```c
// kronkflow: src/scheduler/scheduler_update.c, kfScheduler_tick
++sch->tick;
while (sch->count > 0 && sch->tasks[0].target <= sch->tick) {    // every task that is due...
    ctask = sch->tasks[0];
    prMinHeap_remove(sch, 0);
    push_to_buckets(sch, &ctask);                                 // ...goes into the bucket of its stage
}
for (size_t i = 0; i < kuDynarray_getLoad(sch->staged); ++i) {   // stages, from 0 up
    for (size_t j = 0; j < kuDynarray_getLoad(sch->staged[i]); ++j) {
        ctask = sch->staged[i][j];
        if (ctask.cancelled) { ... free it; continue; }           // removed during this step
        r = ctask.handler(context, ctask.data);                   // handle(world)
        if (r && ctask.interval > 0 && !sch->staged[i][j].cancelled) {
            ctask.target = sch->tick + ctask.interval;            // again, `interval` steps later
            kfScheduler_insertTask(sch, &ctask);
        } else if (ctask.clearer) {
            ctask.clearer(ctask.data);                            // returned false: the system is deleted
        }
    }
    kuDynarray_clear(sch->staged[i]);
}
```

The rules that follow from it:

- **Stages run in increasing order.** `kuge::stage::Network` is 0 and `Render` is 6. A stage is only a bucket number.
- **Inside a stage, systems run in the order they were added.** Due tasks leave the heap in `(target, id)` order,
  and ids grow with each `addSystem`.
- **`handle` returning `false` removes the system**, and deletes it.
- **`delay`** is when it first runs (1, the default, means the next step). **`interval`** is how often it runs.
- **Nothing runs in parallel.** The read/write masks are stored, and no one reads them. Inside a World, systems run
  one after the other on the thread that runs the scene. KUGE's parallelism is between scenes (step 7).
- **A system can be removed while a step runs** (`removeSystem`): it is flagged `cancelled`, skipped, and freed.

## KUGE's layer: exceptions, and removal at the end

`Scene::addSystem` does not give your system to the World directly. It wraps it:

```cpp
// modules/core/src/Scene.cpp, GuardedSystem::handle
if (m_error) {
    return true;                                   // an earlier system of this pass threw: skip, stay scheduled
}
try {
    const bool again = m_system->handle(world);
    ...
    return again;
} catch (...) {
    m_error = std::current_exception();            // kept by the scene
    return true;
}
```

```cpp
// modules/core/src/Scene.cpp, Scene::fixedTick
m_world->getResource<Time>() = time;
m_world->runOnce(kw::Schedule::Fixed);
rethrowError();                                    // thrown again, now that the C code is out of the way
```

An exception must not go through the C scheduler: it would leave its step half done, with `ticking` still set and a
bucket not cleared. So each system is wrapped. The first exception is caught and kept, the systems after it in that
pass are skipped, and once `runOnce` has returned normally, the exception is thrown again. It then comes out of
`step()` (or, for a spawned scene, is logged and stops it: step 7).

The scene also keeps the handle of every system it added. When it is left, `Scene::shutdown` removes them, last
added first, **while the World and its resources still exist**. A system that holds something (a texture, a
connection) can release it before the World goes away.

## Lab

`example/lab/lab04_scheduler.cpp`:

```cpp
// Lab 4: in what order systems run. Straight on a kw::World, with no engine around it.
#include "kronkworld/Kronkworld.hpp"
#include <cstdio>
#include <memory>

namespace
{
    class Say final : public kw::ISystem
    {
        public:
            Say(const char* text, bool again = true) : m_text(text), m_again(again) {}
            bool handle(kw::World&) override { std::printf(" [%s]", m_text); return m_again; }

        private:
            const char* m_text;
            bool        m_again;
    };
}

int main()
{
    kw::World world;

    //                    schedule          stage  system                                   delay interval
    world.scheduleSystem(kw::Schedule::Fixed, 2, std::make_unique<Say>("B: stage 2"));
    world.scheduleSystem(kw::Schedule::Fixed, 0, std::make_unique<Say>("A: stage 0"));
    world.scheduleSystem(kw::Schedule::Fixed, 2, std::make_unique<Say>("C: stage 2, after B"));
    world.scheduleSystem(kw::Schedule::Fixed, 1, std::make_unique<Say>("D: every 3rd"),        1,    3);
    world.scheduleSystem(kw::Schedule::Fixed, 1, std::make_unique<Say>("E: once", false));
    world.scheduleSystem(kw::Schedule::Fixed, 3, std::make_unique<Say>("F: from tick 4"),      4,    1);
    world.scheduleSystem(kw::Schedule::Frame, 0, std::make_unique<Say>("G: Frame schedule"));

    for (int tick = 1; tick <= 5; ++tick) {
        std::printf("Fixed tick %d:", tick);
        world.runOnce(kw::Schedule::Fixed);
        std::printf("\n");
    }
    std::printf("Frame:");
    world.runOnce(kw::Schedule::Frame);
    std::printf("\n");
}
```

What it prints (`./build/example/lab04_scheduler`):

```text
Fixed tick 1: [A: stage 0] [D: every 3rd] [E: once] [B: stage 2] [C: stage 2, after B]
Fixed tick 2: [A: stage 0] [B: stage 2] [C: stage 2, after B]
Fixed tick 3: [A: stage 0] [B: stage 2] [C: stage 2, after B]
Fixed tick 4: [A: stage 0] [D: every 3rd] [B: stage 2] [C: stage 2, after B] [F: from tick 4]
Fixed tick 5: [A: stage 0] [B: stage 2] [C: stage 2, after B] [F: from tick 4]
Frame: [G: Frame schedule]
```

## Reading the output

- **Stage order:** A (stage 0) first, then D and E (stage 1), then B and C (stage 2), then F (stage 3). The order
  of the `scheduleSystem` calls does not matter between stages.
- **Insertion order within a stage:** B before C, and D before E: in the order they were added.
- **E** returned `false` at its first run, so it was deleted: it never appears again.
- **D** (`interval` 3) runs at ticks 1 and 4. **F** (`delay` 4) starts at tick 4.
- **G**, in the Frame schedule, ran only when the Frame scheduler was stepped. The two schedulers know nothing of
  each other.

Next: [Step 5: Scenes](../05-scenes/README.md).
