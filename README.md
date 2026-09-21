# KUGE

KUGE is a C++20 game engine made of independent building blocks: a game links the ones
it needs and nothing else. A solo game, a dedicated server and a game where one player
hosts the match are all built from the same blocks.

```
                 +-----------------+
   your game --->|   kuge-client   |  window, inputs, 2D drawing, animations, tilemaps, sounds, text, UI
                 +--------+--------+
                          |
   your game --->+--------v--------+   +------------------+
                 |    kuge-core    |<--|  kuge-physics    |  2D collisions, triggers, queries
                 +--------+--------+   +------------------+
                          |
             kronkworld (ECS)  kronkflow (scheduler)  kronkpool (threads)
```

`kuge-client` and `kuge-physics` only depend on `kuge-core`, and never on each other's
internals. A server links `kuge-core` (and `kuge-physics`) without SDL, a window or any
drawing code: the build system makes it impossible to include what you did not link.

## Status

| Part | State |
|---|---|
| Engine, scenes, fixed timestep | done |
| Serialization, config files | done |
| Client: window, inputs, keybinds, 2D drawing (SDL2) | done |
| Physics 2D, tilemaps, animations | done |
| Save/load (slots, snapshots), user folders | done |
| Audio (buses, sounds at a place, music), fonts and text, UI (menus, HUD) | done |
| A complete solo game (`kuge_platformer`) | done |
| Hot reload of assets | not done |
| Multithreading (scenes on threads) | planned |
| Network, server, replication | planned |
| 3D (kronk3d) | planned |

Everything listed as *done* is covered by tests (see [Tests](#tests)).

## Build

Requirements: a C++20 compiler (GCC 13 is what it is developed with), CMake 3.24 or later,
and, for the client module, SDL2, SDL2_mixer and SDL2_ttf (the core and the physics do not
need them). A machine with no sound card still runs a game: it is just silent.

```sh
git clone --recurse-submodules <url> kuge && cd kuge
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

| Option | Default | What it does |
|---|---|---|
| `KUGE_BUILD_CLIENT` | `ON` | Build `kuge-client` (needs SDL2, SDL2_mixer, SDL2_ttf). Turn it off for a server. |
| `KUGE_BUILD_SHARED` | `ON` | Also build `kuge.so`, every module in one shared library. |
| `KUGE_BUILD_EXAMPLE` | `ON` | Build the examples. |
| `KUGE_BUILD_TESTS` | `ON` | Build the tests. |
| `KUGE_SANITIZE` | empty | `address` (with UBSan) or `thread`. |

Dependencies are pinned: `vendor/` holds the submodules (kronkworld, kronkpool, kronknet,
kronk3d), and kronkflow, kronklab and stb are fetched at a fixed commit. Only one copy of
kronkpool is built, the vendored one.

### Using KUGE from a game

Link the modules you use; each one is a CMake target.

```cmake
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE kuge-client kuge-physics)   # brings kuge-core too
```

A dedicated server would link `kuge-core` and `kuge-physics` only.

### Examples

| Program | What it shows |
|---|---|
| `build/example/kuge_example` | A headless scene that counts ticks. Ctrl+C ends it cleanly. |
| `build/example/kuge_pong` | Pong for two players. W/S and Up/Down, Space to serve, Esc to quit. `keybinds.cfg` is written on the first launch: edit it to rebind. |

| `build/example/kuge_platformer` | A platformer that uses everything: tilemap, animations, physics, HUD text, menus, sounds and saves. A/D or arrows, Space to jump, Esc to pause (Resume / Save game / Main menu). Collect the 8 coins; avoid the blobs. `--font <file.ttf>` picks the font. |

`kuge_pong --frames 90 --screenshot out.ppm` plays a scripted match without waiting and
saves a picture of the last frame (`kuge_platformer` takes the same options). It is how the game is tested on a machine with no screen
(`SDL_VIDEODRIVER=dummy`).

## How the engine works

### The loop

The simulation runs at a **fixed rate** (60 ticks per second by default), whatever the
speed of the machine. Each loop of the engine does:

1. `beginFrame()` of every module (the client reads the window events and the inputs);
2. as many **fixed ticks** as the elapsed time asks for (at most `maxCatchUp`, so that a slow
   machine does not spiral down);
3. one **frame**;
4. `endFrame()` of every module (the client shows what was drawn);
5. the scene changes that were requested.

Systems are put in one of two schedules:

- **Fixed**: runs at each tick. Gameplay, physics, animations: everything that must give the
  same result on every machine.
- **Frame**: runs once per loop. Drawing.

Inside a schedule, systems run in **stages**, in this order (`kuge::stage`):

| Stage | For |
|---|---|
| `Network` | receive from the network |
| `Input` | read inputs, apply received actions |
| `Simulation` | movement wishes, AI, game rules |
| `Physics` | things move and collide |
| `Late` | react to what the physics found, damage, animations |
| `Replication` | send the state to the clients |
| `Render` | draw (Frame schedule) |

Systems of the same stage run in the order they were added.

Because the drawing happens more often than the ticks, an entity that has a
`PreviousTransform2D` is drawn **between** its last two positions (`Time::alpha` says how far
the frame is between two ticks), so movement stays smooth on a 144 Hz screen.

### Scenes

A **scene** is a part of a game: a menu, a level, a pause screen. It owns its own ECS `World`
(from kronkworld), and says what it does by adding systems. It never says where or when it
runs. The engine keeps a stack of scenes and only the top one runs.

```cpp
class Hello : public kuge::Scene
{
    public:
        void onEnter() override
        {
            addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Ticker>());
        }
        void onExit() override { /* the systems are removed right after */ }
};

int main()
{
    kuge::Engine engine({.mode = kuge::Engine::Mode::Headless, .tickRate = 60});

    engine.scenes().change<Hello>();     // constructor arguments can follow
    return engine.run();                 // until stop(), Ctrl+C, or no scene is left
}
```

`change<T>()` replaces the whole stack, `push<T>()` puts a scene over the current one (which
gets `onPause`/`onResume`), `pop()` leaves the top one. These are **deferred**: a scene is
never destroyed while it runs. They happen between two loops, in the order they were asked.

A **system** is a class deriving from `kw::ISystem` whose `handle(kw::World&)` is called
once per tick (or frame). It reads and writes components:

```cpp
class MoveRight : public kw::ISystem
{
    public:
        bool handle(kw::World& world) override
        {
            const float dt = static_cast<float>(world.getResource<kuge::Time>().dt);
            auto view = world.view<kuge::Transform2D>();

            for (kw::Entity entity : view) {
                world.get<kuge::Transform2D>(entity).position.x += 100.0f * dt;
            }
            return true;          // false: not called again
        }
};
```

`kuge::Time` is a resource of every scene's World: `dt` (the fixed step), `tick`, `alpha`
(Frame only) and `frameDt`.

### Modules

A **module** is a part of the engine that a game adds, without the core knowing it. It gets
hooks around each loop and gives resources to the World of every scene that is entered:

```cpp
kuge::Engine engine({.mode = kuge::Engine::Mode::Windowed});
auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window));
```

The engine owns its modules, and destroys them after the scenes (last added first). Services
reach systems as `kuge::Ref<T>` resources (a pointer that lives in the World):
`world.getResource<kuge::Ref<kuge::InputMap>>()->isDown(...)`.

### Data and resources

- **Components** are plain structs (`Transform2D`, `Sprite`, `Collider`...). Add them with
  `world.add<C>(entity, C{...})` (aggregate initialization).
- **Resources** are one per type and per World (`world.addResource<R>()`,
  `world.getResource<R>()`). The engine puts `Time` in each World; modules add theirs.
- The ECS holds at most 256 component types and 256 resource types per process.
- **Entity order**: a `world.view<...>()` visits entities in the order the ECS stores them,
  which changes when entities are removed. Anything that must be reproducible (physics,
  drawing order, events) sorts by entity number first, and so should your systems.

## Core module (`kuge-core`)

| Header | What it gives |
|---|---|
| `Engine.hpp` | The loop. `Config{mode, tickRate, maxCatchUp, maxFps}`, `run()`, `stop()` (any thread), `step(seconds)` (one loop, for tests), SIGINT/SIGTERM handled. |
| `Scene.hpp`, `SceneManager.hpp` | Scenes and their stack. `Scene::setup()` gives a `SceneSetup` to functions that install systems (`installPhysics`). |
| `Module.hpp`, `Ref.hpp` | Modules and `Ref<T>` resources. |
| `Time.hpp`, `Stage.hpp`, `FixedTimestep.hpp` | The clock, the stages, and the accumulator (which knows nothing about clocks: it is tested by giving it frame durations). |
| `Math2D.hpp`, `Transform2D.hpp` | `Vec2`, `Rect` (y points down, angles are degrees, clockwise), `Transform2D`, `PreviousTransform2D`. |
| `Serializer.hpp` | `ByteWriter` / `ByteReader`: binary data, little-endian, the same on every machine. Everything read is checked (sizes announced by the data are verified before allocating). Versioned headers (`writeHeader`/`readHeader`), atomic file writes (`writeFile`). |
| `ConfigFile.hpp` | `key = value` files with `[sections]`, typed reads with fallbacks, alphabetical output. |
| `TileMap.hpp` | A level made of tiles, as data (see [Tilemaps](#tilemaps)). |
| `AssetManager.hpp` | Loads files once and shares them while somebody holds them. |
| `Save.hpp` | `SaveSlots`: named, versioned, checksummed save files (see [Saves](#saves)). |
| `Snapshot.hpp` | `SnapshotRegistry`: what of a `World` goes in a save, and how. |
| `UserDirectory.hpp` | `userDirectory(UserDir::Config or Data, game)`: where the player's files live. |

The logger (`kuge-logger`, `Logger::logger().info("...{}", x)`) is a separate module.

## Client module (`kuge-client`)

Add it to a **windowed** engine. It gives each scene these resources:
`Ref<IWindow>`, `Ref<IRenderer2D>`, `Ref<InputMap>`, `Ref<AssetManager<Texture>>`,
`Ref<Audio>` with `Ref<AssetManager<Sound>>` and `Ref<AssetManager<Music>>`,
`Ref<TextRenderer>`, `ActionState`, `Camera2D`, `WhitePixel` and `AnimationEvents`.

A scene that draws derives from `ClientScene` and calls `installClientSystems()` first in
`onEnter()`. That adds, in this order:

- Fixed, `Input`: `SampleInput` (the player's actions for this tick) and
  `SnapshotTransforms` (remember where things were);
- Fixed, `Late`: `AnimateSprites`;
- Frame, `Render`: `SpriteRender` (sprites and tilemaps).

### Backends

The client does not talk to a library directly. It talks to three interfaces:
`IWindow`, `IInputSource` and `IRenderer2D`, gathered in a `Backend`, plus two optional
ones: `IAudio` and `IFontLoader` (a backend without them gives a game with no sound and no text).

- `makeSdlBackend(WindowConfig)` is the default one (SDL2: window, keyboard, mouse,
  gamepads, drawing on the GPU, or on the CPU when there is none; sounds and music through
  SDL_mixer, fonts through SDL_ttf). If there is no audio device, the game runs silently and
  says so in the log.
- `makeDummyBackend()` draws nothing and **records** what it was asked to draw. It is how
  the client and the games are tested (`tests/client/client_fixture.hpp`).
- A game that prefers SFML, or anything else, implements the interfaces and gives them
  to `ClientModule`.

### Inputs and keybinds

The game never sees a key: it sees **actions**, defined as an enum (up to 64).

```cpp
enum class Action : std::uint8_t { Up, Down, Shoot };

input.declare(Action::Shoot, "shoot");                       // its name in the keybinds file
input.bind(Action::Shoot, kuge::Key::Space);
input.bind(Action::Shoot, kuge::GamepadButton::A);           // several inputs, one action
input.bind(Action::Up, kuge::Binding::axis(kuge::GamepadAxis::LeftY, -1));
input.loadBindings("keybinds.cfg");                          // what the player changed wins
```

Each fixed tick, `SampleInput` puts an `ActionState` in the World: which actions are
`isDown`, `wasPressed` and `wasReleased`. A key pressed and released between two ticks still
counts as pressed. The simulation reads only this:

```cpp
const auto& actions = world.getResource<kuge::ActionState>();
if (actions.wasPressed(Action::Shoot)) { ... }
```

Keys are named after their place on the keyboard (`W` is where WASD games put it, on any
layout). Rebinding in a menu: `input.startCapture()`, then `input.takeCaptured()` gives the
next input the player pushed. The file looks like this (comments start with `#`):

```
input.shoot = Space, Pad.A
input.up    = W, Pad.DPadUp, Pad.LeftY-
input.pause =                       # empty: nothing bound to it
```

Only the actions the file mentions are replaced: the defaults survive a file with a few lines.
`Pad.LeftY-` is a stick pushed the negative way; `Mouse.Left` is a mouse button.

### Sprites, camera, drawing

An entity with a `Transform2D` and a `Sprite` is drawn. World units are pixels at zoom 1,
y points down, the rotation is in degrees. A sprite without a texture is a plain rectangle of
its tint.

```cpp
world.add<kuge::Transform2D>(e, kuge::Transform2D{{100.0f, 50.0f}});
kuge::Sprite sprite;
sprite.size = {32.0f, 32.0f};
sprite.tint = kuge::colors::Red;
sprite.layer = 2;                      // higher layers are over the lower ones
world.add<kuge::Sprite>(e, sprite);
```

`Camera2D` (a resource: `position`, `zoom`) says what the screen shows. Drawing is sorted by
`(layer, z, texture, entity)`: always the same order, whatever the ECS does. Sprites outside
the screen are skipped. Load a picture with
`client.textures().load("hero.png")` (PNG, JPEG, BMP, TGA, GIF through stb_image): the same
file gives the same texture while somebody holds it.

A `Texture` must not outlive the renderer: keep textures in components (destroyed with the
scene), not in globals.

### Spritesheets and animations

A `Spritesheet` is a picture cut in equal cells (frames of an animation, tiles of a tileset),
numbered from 0, row after row. Animations are described in a text file, one clip per line:

```
# clip <name> fps=<n> frames=<list> [loop=false] [next=<clip>] [cue.<frame>=<name>]
clip idle   fps=6  frames=0-3
clip run    fps=12 frames=4,5,6,7,6,5
clip attack fps=14 frames=8-11 loop=false next=idle cue.2=hit
```

```cpp
auto clips = std::make_shared<kuge::AnimationSet>(kuge::AnimationSet::load("hero.anim"));
world.add<kuge::Sprite>(e, kuge::Sprite{});
world.add<kuge::Animator>(e, kuge::Animator::of(sheet, clips, "idle"));

world.get<kuge::Animator>(e).play("run");      // nothing happens if it already runs
```

`AnimateSprites` moves the animators forward by one tick (in the Fixed schedule, so the
animation is part of the simulation) and gives the `Sprite` the picture of the frame that is
due. A clip that does not loop and has a `next` goes there when it ends: clips form a small
state machine. `cue` names are given through the `AnimationEvents` resource, once each, in
the order of the entities, even if a tick jumps over several frames:

```cpp
for (const auto& event : world.getResource<kuge::AnimationEvents>().list) {
    if (event.name == "hit") { /* the sword swings: apply the damage */ }
}
```

### Tilemaps

`kuge::TileMap` (in the core) is a level as **data**: a size, layers of tile numbers (0 is
"nothing"), and which tiles are solid. A server can load one to know where the walls are.

```
kuge-tilemap 1
size 8 3                 # in tiles
tilesize 16              # in pixels
solid 1 2                # tiles that stop bodies
layer ground
1 1 1 1 1 1 1 1          # one row per line, numbers apart by spaces or commas
0 0 0 0 0 0 0 0
2 2 0 0 0 0 2 2
layer sky hidden         # present, but not drawn
0 0 0 0 0 0 0 0
0 0 0 0 0 0 0 0
0 0 0 0 0 0 0 0
```

```cpp
auto map = std::make_shared<kuge::TileMap>(kuge::TileMap::load("level1.tilemap"));
world.add<kuge::Transform2D>(e, kuge::Transform2D{});
world.add<kuge::TilemapView>(e, kuge::TilemapView{map, tilesSheet, "ground", /*layer*/ -10});
```

One `TilemapView` draws one layer of the map (or all the visible ones if the name is empty),
only the tiles that show on the screen, with no seam between tiles at any zoom. It is
sorted with the sprites by `layer`, so a hero can walk between two layers of tiles. Tile
number `n` is the cell `n - 1` of the sheet.

### Audio

`Ref<Audio>` (or `client.audio()`) is what a game plays through. Sounds and music come from
`client.sounds().load("boom.wav")` / `client.musics().load(...)` (loaded once, shared).

```cpp
audio.play(*boom);                                          // {.volume, .pan, .loop}
audio.playAt(*boom, enemyPosition, {.position = camera});   // louder when near, panned by where it is
audio.setBusVolume(kuge::Bus::Music, 0.4f);                 // Effects and Music, under the master volume
audio.playMusic(*theme, {.fadeIn = 2.0f});                  // one music at a time
```

The volume heard is master x bus x the sound's own. `DummyAudio` (in `makeDummyBackend()`)
counts the voices and records what was played, so a test can check that a coin made its
sound. A `Sound` must not outlive the backend that made it.

### Fonts and text

`client.loadFont("font.ttf", 14)` gives an `IFont` (shared while somebody holds it):
`measure(text, wrapWidth)`, `rasterize`, `lineHeight`. Text is UTF-8, `\n` starts a line, and a
wrap width cuts lines at spaces. `Ref<TextRenderer>::draw(font, text, position, color)` draws
text in screen pixels: each text is turned into a picture once and kept while it keeps being
drawn (forgotten after 120 frames, 1024 at most).

### User interface

Menus, HUD and options are entities in **screen pixels** (the camera does not move them).
Call `installUi(setup(), actions)` after `installClientSystems()`.

| Component | What it is |
|---|---|
| `UiNode` | A rectangle: `anchor` (9 points) + `offset` in its `parent` (or the screen), `size` (0: as big as its content), `visible`, `layer`. |
| `UiStack` | On a parent: lays its children out as a column or a row (`spacing`, `padding`, `align`). |
| `UiPanel` | A background and a border. |
| `UiLabel` | Text (font from the theme unless given), color, wrap width, alignment. |
| `UiButton` | Text, `enabled`; `hovered` / `focused` / `pressed` are filled in by the interaction. |

Resources: `UiTheme` (font and colors, change it for your game), `UiActions` (which of *your*
actions mean up/down/left/right/accept/cancel/click), `UiState` (what has the focus),
`UiEvents` (what happened in the last tick: `Activated`, `Focused`, `Cancelled`) and
`UiLayoutResult`.

The systems: layout (Frame, `Late`), interaction (Fixed, `Input`: nearest-neighbour focus
navigation with the keyboard or gamepad, mouse hover and click) and drawing (Frame, `Render`,
after the sprites). A menu is therefore: build the entities, then a system that reads
`UiEvents` and acts (see `MenuLogic` in `example/platformer/Platformer.hpp`). Layout is one
frame behind what you just changed.

### Saves

Three small pieces, all in `kuge-core` (no window needed):

- **`userDirectory(UserDir::Data, "mygame")`** is where the player's files go
  (`$XDG_DATA_HOME` or `~/.local/share`, and `Config` for settings), created if needed.
- **`SnapshotRegistry`** says what of a `World` is saved and how. Register each component
  and resource type with a name and two lambdas (write, read), and mark entities with
  `Persistent`. `save(world, writer)` writes them (entities by increasing number, so the same
  world always gives the same bytes); `load(world, reader)` creates them again. Nothing else is
  saved, so a save never holds a texture or a pointer. A name the reader does not know is
  skipped (a newer save still loads), and a damaged one loads **nothing**: it is checked first.
- **`SaveSlots(directory, gameName, version)`** keeps the bytes in named slots (`.ksave`):
  `write(slot, label, bytes)`, `read`, `list` (to fill a "Continue" menu), `exists`, `remove`.
  A file holds the game's name, the version that wrote it, a label and a CRC-32. Reading tells
  apart `Missing`, `Corrupt`, `WrongGame`, `TooNew` (`SaveError::reason()`); an older version is
  read and its version is given, so the game can convert. Writes are atomic.

```cpp
kuge::ByteWriter out;
registry.save(world, out);
saves.write("slot1", "3 of 8 coins", out.bytes());
...
auto data = saves.read("slot1");
kuge::ByteReader in(data.payload);       // (a temporary would dangle: this does not compile)
registry.load(world, in);
```

### The platformer demo

`example/platformer/` is a whole game in a header and a `main.cpp`, with no art files (the
tiles, hero, coins and sounds are drawn and synthesised in `Art.hpp`). Three scenes:
`MenuScene` (New game / Continue / Quit; Continue is disabled without a save), `GameScene`
(60x15 tile level, gravity, jump, walkers that turn at walls, coins as triggers, HUD, a
camera that follows and stops at the ends of the level) and `PauseScene`, **pushed** on the
game (Resume / Save game / Main menu). Continue restores the hero, the coins that are left
and the counter. It is the model to read to see how the modules fit together, and it is what
`tests/client/platformer_test.cpp` plays: on the dummy backend, tick by tick.

## Physics module (`kuge-physics`)

It needs the core only, so a server has it too. Add it to a scene:

```cpp
void Level::onEnter()
{
    kuge::installPhysics(setup(), {.gravity = {0.0f, 1200.0f}});
    world().getResource<kuge::Physics2D>().tiles = kuge::makeSolidGrid(map, "ground");
    ...
}
```

`installPhysics` adds the `Physics2D` resource and a system in the `Physics` stage: your
systems in `Simulation` say where things want to go, the physics moves them, and your systems
in `Late` react to what happened.

| Component | Meaning |
|---|---|
| `Collider` | A box or a circle, with an offset, `layer` and `mask` (what it is, and what it meets), and a `trigger` flag. Needs a `Transform2D`. |
| `Body` | Makes it move: `Kinematic` (by its `velocity`) or `Dynamic` (also pulled by gravity). No `Body`, or `Static`: a wall. |

```cpp
world.add<kuge::Collider>(floor, kuge::Collider::box(400.0f, 20.0f));
world.add<kuge::Collider>(hero,  kuge::Collider::box(12.0f, 24.0f));
world.add<kuge::Body>(hero, kuge::Body{kuge::Body::Type::Dynamic});   // velocity, gravityScale...
world.add<kuge::Collider>(coin,  kuge::Collider::circle(8.0f).asTrigger());
```

What it does, and does not:

- Bodies move along x then along y, in steps smaller than themselves, so a fast body cannot
  cross a thin wall, and they **slide** along walls. `body.contacts` says which sides touch
  a wall (a collider with no body, or a solid tile), even when the body stands still.
- Bodies are **boxes**; circles are walls, triggers and things to query. Bodies do **not**
  stop each other: to know who touches whom, use a trigger.
- Triggers stop nothing: `physics.events()` lists who **entered** or **left** during the last
  tick (a destroyed entity does not "leave").
- Questions: `raycast`, `overlapRect`, `overlapCircle`, `tiles.isSolidAt`. They describe the
  world as the last tick left it (nothing is known before the first tick).
- It is **deterministic**: entities are always handled in increasing order, so the same world
  gives the same result bit for bit, whatever the ECS does with its storage.
- No one-way platforms, no rotation, no body-body resolution (for now).

## File formats

All are text, meant to be edited by hand, and a mistake names the line it is on.

| File | Read with | Written with |
|---|---|---|
| `.cfg` (config, keybinds) | `ConfigFile::load`, `InputMap::loadBindings` | `ConfigFile::save`, `InputMap::saveBindings` |
| `.tilemap` | `TileMap::load` | `TileMap::save` |
| `.anim` | `AnimationSet::load` | (by hand) |
| `.ksave` (binary) | `SaveSlots::read` | `SaveSlots::write` |

Saves and network messages use the binary `ByteWriter` / `ByteReader`. Files are written
atomically (a temporary file, then a rename), so a crash never leaves half a file.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

| Program | Covers |
|---|---|
| `kuge_tests` | core: loop, scenes, modules, serializer, config, tilemaps, assets, saves, snapshots |
| `kuge_client_tests` | client: inputs, sprites, tiles, animations, audio, text, the UI (layout, focus, mouse, drawing), the SDL backend (on SDL's dummy screen and audio, reading real pixels), and the game logic of Pong and of the platformer |
| `kuge_physics_tests` | physics: shapes, broad phase, scenarios, a property test, bit-for-bit replay |
| `boundary_*` | a module cannot include what it does not link |
| `pong_smoke`, `platformer_smoke` | the real binaries play a scripted game and leave a picture that is checked |

Things that are worth knowing:

- **kronklab** (the test library of kronk*) always exits with 0, so `ctest` reads its report
  line instead (`cmake/RunKronklab.cmake`). Test names are limited to 31 characters.
- Tests that protect against a specific mistake were checked by **breaking the protection on
  purpose** and seeing the test fail (sub-stepping and the contact skin in the physics, the
  seam-free tile edges in the drawing).
- Sanitizers: `cmake -S . -B build-asan -DKUGE_SANITIZE=address` (also UBSan) and
  `-DKUGE_SANITIZE=thread` (on a recent kernel, run the tests with `setarch "$(uname -m)" -R`).
  Everything is clean under both.

## Project layout

```
CMakeLists.txt        options, pinned dependencies, modules
cmake/                KugeModule.cmake (kuge_module, kuge_vendor, kuge_add_test), test helpers
modules/logger/       kuge-logger
modules/core/         kuge-core
modules/physics/      kuge-physics
modules/client/       kuge-client (input/, render/, animation/, audio/, ui/, backend/, sdl/)
modules/net/          kuge-net (not finished)
example/              the headless example, pong/ and platformer/
tests/                one folder per module, and the boundary tests
vendor/               submodules: kronkworld, kronkpool, kronknet, kronk3d
```

Each module is a static library (`kuge-core`...) built from an object library, which the
optional `kuge.so` reuses. To add one: a `CMakeLists.txt` with
`kuge_module(name DEPENDS kuge-core ...)` and its sources in `src/`.
