# 01 Getting started

## What you need

- A C++20 compiler (GCC 13 is what it is developed with; on Windows, MSVC from Visual Studio 2022
  17.5 or later), CMake 3.24 or later, and git.
- No library to install by hand: the client module's (SDL2, SDL2_mixer, SDL2_ttf, stb) come from vcpkg,
  which the first `cmake` clones into `.vcpkg/` and which builds them once. The core, the physics,
  the network, the server and the replication need none of them: with `-DKUGE_BUILD_CLIENT=OFF`,
  a server builds with no SDL at all.
- On Linux, the headers SDL is built against (display and sound). On Debian or Ubuntu:

  ```sh
  sudo apt install libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxss-dev \
      libwayland-dev libxkbcommon-dev libegl1-mesa-dev libpulse-dev
  ```

## Build

```sh
git clone --recurse-submodules <url> kuge && cd kuge
cmake -S . -B build -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Without the two options, only the modules are built. Some tests run the examples, so the whole
suite needs both.

On Windows, the build names its configuration:

```powershell
cmake -S . -B build -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

There, `ctest` runs the boundaries and the games played for real, not the unit tests (their test
library, kronklab, runs each test in a `fork()`). The README's "On Windows" says what else differs.

`vendor/` holds submodules (kronkworld, the ECS; kronkpool, the thread pool; kronknet, the sockets;
kronk3d, for later). kronkflow (the scheduler) and kronklab (the test library) are fetched at a
fixed commit. SDL2, SDL2_mixer, SDL2_ttf and stb (image loading) come from vcpkg: `vcpkg.json` lists
them and its `builtin-baseline` fixes their versions. When no toolchain is given and `VCPKG_ROOT`
is not set, `cmake/Vcpkg.cmake` clones vcpkg at that baseline into `.vcpkg/`.

| Option | Default | What it does |
|---|---|---|
| `KUGE_BUILD_CLIENT` | `ON` | Build the client module. Turn it off for a server: no SDL needed. |
| `KUGE_BUILD_SHARED` | `ON` | Also build `kuge.so` (`kuge.dll` on Windows), every module in one shared library. |
| `BUILD_EXAMPLES` | `OFF` | Build the examples. |
| `BUILD_TESTS` | `OFF` | Build the tests. |
| `KUGE_SANITIZE` | empty | `address` (with UBSan) or `thread`. See [testing](../12-testing-and-debugging/README.md). |
| `KUGE_VCPKG_DIR` | `.vcpkg` | Where vcpkg is cloned, when no toolchain is given and `VCPKG_ROOT` is not set. |

## Run the examples

They are in `build/example/`. Each one shows a different part of the engine:

| Program | What it shows |
|---|---|
| `kuge_example` | A headless scene that counts ticks. The smallest program that uses the engine. |
| `kuge_pong` | The client module: window, keybinds, sprites. Two players. |
| `kuge_platformer` | Tilemap, animations, physics, HUD text, menus, sounds, save and load. A solo game. |
| `kuge_chat_server`, `kuge_chat_client` | The server module: a lobby and rooms of four, in the terminal. |
| `kuge_rtype_server`, `kuge_rtype_client`, `kuge_rtype_host` | The game this documentation builds. |

Try the last one first:

```sh
build/example/rtype/kuge_rtype_host          # a game and its own server, in one process
```

Arrows or WASD move, Space fires. Then, in three terminals:

```sh
build/example/rtype/kuge_rtype_server 4242
build/example/rtype/kuge_rtype_client --port 4242 --name Ana
build/example/rtype/kuge_rtype_client --port 4242 --name Ben
```

Every game that has a window takes `--frames N --screenshot file.ppm`: a scripted player plays N
frames as fast as it can and keeps a picture of the last one. It is how the games are tested on a
machine with no screen (`SDL_VIDEODRIVER=dummy`).

## How the repository is laid out

```
modules/logger/       kuge-logger       Logger (thread-safe)
modules/core/         kuge-core         the engine: loop, scenes, modules, threads, serializer, config, assets, saves
modules/physics/      kuge-physics      2D collisions, triggers, queries
modules/client/       kuge-client       window, inputs, 2D drawing, sound, text, UI, tilemaps, animations
modules/net/          kuge-net          messages, endpoints, reliable channels, TCP / UDP / loopback, matchmaking client
modules/server/       kuge-server       a lobby and rooms, with no window
modules/replication/  kuge-replication  snapshots, interpolation, inputs, prediction
example/              the examples above
tests/                one folder per module, and tests that check what a module cannot include
docs/                 what you are reading
vendor/               submodules
```

Which module needs which:

```
                   your game
     ┌──────────────┬─────────────┬───────────────┐
 kuge-client    kuge-server   kuge-replication   (kuge-physics)
     │              │              │
     │              └────── kuge-net ◀────────────┘
     └──────────────┴──────────────┴── kuge-core ── kuge-logger
```

A module only sees the headers of what it links, so **a server that does not link `kuge-client`
cannot include a rendering header**: it does not compile. This is checked by tests
(`boundary_*` in `ctest`), and it is what makes "a server with no window" a fact and not a hope.

## Your first program

The smallest use of the engine (see `example/src/Main.cpp`):

```cpp
#include "Engine.hpp"
#include "Stage.hpp"

class Counter : public kw::ISystem
{
    public:
        bool handle(kw::World& world) override
        {
            std::printf("tick %llu\n", (unsigned long long)world.getResource<kuge::Time>().tick);
            return true;                       // false: not called again
        }
};

class Hello : public kuge::Scene
{
    public:
        void onEnter() override
        {
            addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Counter>());
        }
};

int main()
{
    kuge::Engine engine({.mode = kuge::Engine::Mode::Headless, .tickRate = 60});

    engine.scenes().change<Hello>();
    return engine.run();                        // until Ctrl+C, or no scene is left
}
```

Everything else in these pages is a variation on this: more systems, more scenes, and modules that
give the World more to work with.

Next: [02 How the engine works](../02-how-the-engine-works/README.md).
