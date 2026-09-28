# Inside KUGE: how the engine really works

The other pages explain what KUGE gives you and how to use it. This tutorial opens the engine. Step by step, it
follows the real code paths: what happens in `engine.run()`, how an entity is stored, in what order systems run,
how a message reaches a scene on another thread, what the bytes on the wire are, and how a key you press ends up
moving a ship on someone else's screen.

Each step has three parts:

1. **The mechanism**, explained with the engine's own code, shortened. Every excerpt names its file and its
   function, so you can open the real one next to it.
2. **A lab**: a small program, 40 to 150 lines, that makes the mechanism visible. It prints what the engine does.
3. **Reading the output**: what each line of the lab's output proves.

The outputs shown are real. They were recorded by building and running the labs against KUGE at commit `11314dd`,
three times each, with the same result every time.

## Running the labs

The labs go in `example/lab/`, one file per step. Add this to `example/CMakeLists.txt`:

```cmake
# The labs of "Inside KUGE": one small program per step, built from example/lab/*.cpp
if(TARGET kuge-client AND TARGET kuge-server AND TARGET kuge-replication)
    file(GLOB KUGE_LABS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lab/*.cpp)
    foreach(source ${KUGE_LABS})
        get_filename_component(lab ${source} NAME_WE)
        add_executable(${lab} ${source})
        target_link_libraries(${lab} PRIVATE kuge-client kuge-server kuge-replication)
        target_compile_options(${lab} PRIVATE -Wall -Wextra)
    endforeach()
endif()
```

Then, for the lab of step 2:

```sh
cmake -S . -B build -DBUILD_EXAMPLES=ON      # once: it finds the new files
cmake --build build --target lab02_loop
./build/example/lab02_loop
```

The labs need no window and no sound: the client lab uses the dummy backend, and the network labs use an in-memory
network. Most of them drive time by hand, which is why their output is the same on every machine.

## The steps

| Step | What you learn | Lab |
|---|---|---|
| [1 The map](01-the-map/README.md) | The layers, the libraries underneath, who owns what at run time | none |
| [2 The loop](02-the-loop/README.md) | `run()` and `step()`, the accumulator, `maxCatchUp`, `alpha`, and when the engine sleeps | `lab02_loop` |
| [3 The ECS](03-the-ecs/README.md) | Entities as numbers, sparse sets, signatures, views, and why entity order moves | `lab03_ecs` |
| [4 The scheduler](04-the-scheduler/README.md) | How systems become tasks, stages and order, `delay` and `interval`, exceptions | `lab04_scheduler` |
| [5 Scenes](05-scenes/README.md) | The stack, deferred changes, entering and leaving, mailboxes | `lab05_scenes` |
| [6 Modules](06-modules/README.md) | Hooks, injection, `Ref<T>`, and what a module shares with other threads | `lab06_modules` |
| [7 Threads](07-threads/README.md) | One loop per spawned scene, dedicated threads, the tick driver, messages, stopping | `lab07_threads` |
| [8 The client](08-the-client/README.md) | From a key to an `ActionState`, from a `Transform2D` to a draw call, assets | `lab08_client` |
| [9 Physics](09-physics/README.md) | Sub-steps, the skin, contacts, triggers, and determinism | `lab09_physics` |
| [10 Bytes on the wire](10-bytes-on-the-wire/README.md) | The serializer, `KUGE_MESSAGE`, the packet formats, the transports | `lab10_wire` |
| [11 Reliability](11-reliability/README.md) | The handshake, acks and ack bits, resending, round trip time, deferred handlers | `lab11_reliable` |
| [12 Lobby and rooms](12-lobby-and-rooms/README.md) | How a player gets into a room: scenes on threads, tokens, matchmaking | `lab12_server` |
| [13 Replication](13-replication/README.md) | Snapshots as diffs, baselines and acks, the records on the wire, interpolation | `lab13_replication` |
| [14 Inputs and prediction](14-inputs-and-prediction/README.md) | Numbered inputs, the room's queue, reconciling and replaying | `lab14_inputs` |
| [15 One keypress, end to end](15-one-keypress/README.md) | Everything above, in the order a keypress goes through it | none |

Read them in order: each step uses the ones before it. If you only want the network, read 1, 2, 5 and 7, then 10
onwards.

## How to read the excerpts

An excerpt looks like this:

```cpp
// modules/core/src/SceneLoop.cpp, SceneLoop::step
if (!ready() || stop) {
    return false;
}
```

It is shortened: comments of the original may be dropped, and `...` stands for lines left out. The logic is never
changed. When the engine's code changes, the file and function names still lead you to it.
