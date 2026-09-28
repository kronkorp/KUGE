# Step 1: The map

Before following a single code path, know what exists, who depends on whom, and who owns what while the program
runs.

## Three layers of code

```
   your game                 example/rtype, example/platformer, your own...
  ─────────────────────────────────────────────────────────────────────────────────────
   KUGE modules              modules/*, C++20, one static library each
                             kuge-client   kuge-server   kuge-replication
                             kuge-physics  kuge-net      kuge-core      kuge-logger
  ─────────────────────────────────────────────────────────────────────────────────────
   kronk* libraries          vendor/* (git submodules) and fetched ones
                             kronkworld  the ECS (C++ headers)
                             kronkflow   the task scheduler that runs systems (C)
                             kronkpool   a thread pool (C)
                             kronknet    TCP and UDP sockets (C)
                             kronklab    the test library (C)
                             plus SDL2, SDL_mixer, SDL_ttf and stb_image for the client only
```

KUGE does not reimplement what the kronk* libraries do. Its core **wraps** them: a KUGE scene owns a
`kw::World`, KUGE systems are kronkworld systems, `kuge::ThreadPool` is a kronkpool pool, and the socket
transports call kronknet. Step 3 opens kronkworld, step 4 kronkflow, and step 10 kronknet.

## Who depends on whom

Each module's `CMakeLists.txt` is one line, `kuge_module(<name> DEPENDS ...)`:

| Module | Depends on |
|---|---|
| `kuge-logger` | nothing |
| `kuge-core` | `kuge-logger`, `kuge-kronkworld`, `kronkpool` |
| `kuge-physics` | `kuge-core` |
| `kuge-net` | `kuge-core`, `kuge-kronknet` |
| `kuge-server` | `kuge-core`, `kuge-net` |
| `kuge-replication` | `kuge-core`, `kuge-net`, `kuge-physics` |
| `kuge-client` | `kuge-core`, SDL2, SDL2_mixer, SDL2_ttf |

`kuge_module` (in `cmake/KugeModule.cmake`) builds `modules/<name>/src/*.cpp` into an object library, and a
static library from it. Its `src/` folder is a **public include directory of that target only**. So a file sees
the headers of the modules it links, directly or through a `DEPENDS`, and nothing else. `#include
"ClientModule.hpp"` in a server file is not a style mistake the reviewer must catch: it does not compile.

The boundary tests (`tests/boundaries/`) check exactly that. Each one is a file of three lines, built by `ctest`
itself:

```cpp
// tests/boundaries/net_sees_client.cpp: must NOT compile, kuge-net does not depend on kuge-client
#include "ClientModule.hpp"

int main() { return 0; }
```

The test passes only if the build fails **with** `ClientModule.hpp: No such file or directory`, so a failure for
any other reason does not count as a pass.

`kuge.so` (option `KUGE_BUILD_SHARED`) is the same object libraries, gathered in one shared library.

## Who owns what while it runs

```
Engine                                                modules/core/src/Engine.hpp
 ├── modules                vector<unique_ptr<Module>>   destroyed after every scene, last added first
 ├── m_main                 SceneLoop                    the main scenes (the ones engine.scenes() gives)
 │    ├── FixedTimestep     the accumulator (step 2)
 │    ├── Time              the clock its scenes see
 │    └── SceneManager      a stack of unique_ptr<Scene>, and the changes asked but not done yet
 │         └── Scene
 │              ├── kw::World      entities, components, resources, two schedulers (steps 3 and 4)
 │              ├── Mailbox        shared_ptr: the SceneHandles of other scenes keep it alive
 │              └── SceneContext   pointers back to the engine, its SceneManager and its Time
 ├── spawned scenes         each in a SceneLoop of its own (step 7)
 │    ├── m_side            RunPolicy::Main: stepped by the main thread
 │    ├── m_dedicated       RunPolicy::Dedicated: one std::thread each
 │    └── m_driver          RunPolicy::Pooled: a TickDriver that hands ticks to the workers
 └── m_pool                 ThreadPool (kronkpool), made the first time it is needed
```

Three things to notice now, because every later step relies on them:

- **A scene never owns its loop; the loop owns the scene.** A scene does not know whether it runs on the main
  thread or on a worker. What it can reach, it reaches through `ctx()`: the engine, the manager of *its* stack,
  *its* clock, its own handle, and its parent's.
- **Nothing is shared between two scenes.** Each has its own `kw::World`, so its own entities, components,
  resources and systems. The only thing another scene can hold is a `SceneHandle`, which is a
  `shared_ptr<Mailbox>` (step 7).
- **Modules outlive scenes.** `~Engine` first stops every spawned scene, then leaves the main scenes, and only then
  destroys the modules, in reverse order. A scene may therefore hold what a module lent it (step 6).

## The files you will open

| Step | Files |
|---|---|
| 2 The loop | `core/src/Engine.cpp`, `SceneLoop.cpp`, `FixedTimestep.cpp` |
| 3 The ECS | `vendor/kronkworld/include/kronkworld/` (`entity/`, `component/`, `world/`) |
| 4 The scheduler | `kronkworld/system/System.hpp`, kronkflow's `scheduler_update.c`, `core/src/Scene.cpp` |
| 5 Scenes | `core/src/SceneManager.cpp`, `Scene.cpp`, `Mailbox.cpp` |
| 6 Modules | `core/src/Module.hpp`, `Ref.hpp`, `client/src/ClientModule.cpp` |
| 7 Threads | `core/src/Engine.cpp`, `TickDriver.cpp`, `Message.hpp`, `StopSignal.hpp` |
| 8 The client | `client/src/ClientModule.cpp`, `input/InputMap.cpp`, `render/Systems.cpp`, `core/src/AssetManager.hpp` |
| 9 Physics | `physics/src/Physics2D.cpp`, `SpatialGrid.cpp` |
| 10 Bytes on the wire | `core/src/Serializer.*`, `net/src/Wire.hpp`, `Endpoint.cpp`, `Loopback.cpp`, `SocketTransport.cpp` |
| 11 Reliability | `net/src/Endpoint.cpp`, `Reliable.cpp` |
| 12 Lobby and rooms | `server/src/GameServer.cpp`, `Lobby.cpp`, `RoomScene.cpp`, `net/src/Matchmaking.cpp` |
| 13 Replication | `replication/src/Replication.*`, `WorldState.cpp`, `ReplicationServer.cpp`, `ReplicationClient.cpp` |
| 14 Inputs and prediction | `replication/src/Input.hpp`, `Prediction.hpp` |

(All the paths above are under `modules/`, except the ones under `vendor/`.)

Next: [Step 2: The loop](../02-the-loop/README.md).
