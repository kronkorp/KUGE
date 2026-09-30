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

## Documentation

The [`docs/`](docs/README.md) folder explains the engine module by module, then builds a whole game with it: a small
R-Type, as a dedicated server, a client and a host, from an empty folder to tests
([start here](docs/11-make-rtype/README.md)). This README is the reference.

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
| Hot reload of textures, sounds and musics | done |
| Multithreading: scenes on threads, messages, workers, background loading | done |
| Network: transports (TCP, UDP, in-memory), reliable messages | done |
| Server: lobby, rooms, sessions and tokens, matchmaking client | done |
| Replication (snapshots, interpolation) and client-side prediction | done |
| One game, three ways (R-Type: server, client, host), with no window on the server | done |
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
```

That builds the modules only. For the examples and the tests (some tests run the examples,
so the whole suite needs both):

```sh
cmake -S . -B build -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

| Option | Default | What it does |
|---|---|---|
| `KUGE_BUILD_CLIENT` | `ON` | Build `kuge-client` (needs SDL2, SDL2_mixer, SDL2_ttf). Turn it off for a server. |
| `KUGE_BUILD_SHARED` | `ON` | Also build `kuge.so`, every module in one shared library. |
| `BUILD_EXAMPLES` | `OFF` | Build the examples. |
| `BUILD_TESTS` | `OFF` | Build the tests. |
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

Built with `-DBUILD_EXAMPLES=ON`.

| Program | What it shows |
|---|---|
| `build/example/kuge_example` | A headless scene that counts ticks. Ctrl+C ends it cleanly. |
| `build/example/kuge_pong` | Pong for two players. W/S and Up/Down, Space to serve, Esc to quit. `keybinds.cfg` is written on the first launch: edit it to rebind. |

| `build/example/kuge_chat_server`, `kuge_chat_client` | A chat over the network: a server with no window (lobby and rooms of four), and a terminal client. `kuge_chat_client Ana --say hello --wait 2` says it once and leaves. |
| `build/example/kuge_rtype_server [port]`, `kuge_rtype_client`, `kuge_rtype_host` | R-Type, the small one: up to four ships shoot waves of enemies. The **server** has no window, the **client** connects to it, the **host** runs the room and the window in one process (see [R-Type](#r-type-one-game-three-ways)). Arrows or WASD, Space to fire. `--frames N --screenshot f.ppm` plays a scripted pilot. |
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

A system that throws ends that pass (the systems after it do not run) and the exception comes
out of the scene's tick or frame, so `run()` and `step()` report it; nothing is left half done.

`kuge::Time` is a resource of every scene's World: `dt` (the fixed step), `tick`, `alpha`
(Frame only) and `frameDt`.

### Threads

Only the scenes of the main stack run on the thread that calls `run()`. Others can be
**spawned**, with a policy that says where they run:

| `RunPolicy` | Runs |
|---|---|
| `Main` | On the main thread, in the loop of the engine, next to the main scenes (a HUD, a host's game). |
| `Dedicated` | On a thread of its own, with its own loop at the tick rate (a server room, a lobby). |
| `Pooled` | On the worker threads: a driver works out when each scene is due and hands its tick to a worker. Many scenes on few threads. |

```cpp
kuge::SceneHandle room = ctx().spawn<RoomScene>(kuge::RunPolicy::Dedicated, settings);
room.send(StartMatch{});     // from any thread
...
room.stop();                 // it is popped, at the start of its next loop
```

The scene is built by the caller with the arguments (they are copied or moved into it), and
entered, ticked and left by the thread that runs it. `ctx().spawn` makes the caller its
**parent**: `ctx().parent()` (in the spawned scene) is a handle to send answers to.
`engine.spawn<T>()` does the same from outside a scene (no parent).

**Nothing is shared between scenes**, whatever thread they run on: each has its own `World`
and loop, and they only talk through **messages**.

- `SceneHandle` (copyable, can outlive the scene) is all you get of another scene:
  `send(value)`, `stop()`, `alive()`. `ctx().self()` is a scene's own handle, to give to others.
- A message is any value (`Message`, moved in, so it may be a type that cannot be copied).
  The receiver gets it in `onMessage(const Message&)`, at the start of each of its loops and
  before its ticks, in the order they were sent, and reads it back by type:
  `if (const auto* joined = message.as<PlayerJoined>()) { ... }`.
- The mailbox is bounded (4096 by default): when it is full, or the scene is gone, `send`
  returns `false` instead of growing without end.
- A paused scene (another one is over it) hears its messages when it runs again.

Good to know:

- **A late scene does not pile up work.** A loop catches up at most `maxCatchUp` ticks (see
  [The loop](#the-loop)); a pooled scene never gets a new tick while its last one still runs.
- **Modules and threads.** A module's `inject()` gives resources to the scenes it is
  entered in. The client's window, renderer and sound device belong to the main thread, so a
  module only injects into the scenes of other threads if it says
  `sharedAcrossThreads() == true` (the default is no). A server room does not get the client.
- **Ending.** When `run()` ends (or the engine is destroyed) every spawned scene is left,
  `onExit()` included, on the thread that ran it, and the threads are joined. Waiting threads
  are woken at once (no waiting for the end of a sleep). A spawned scene that throws is logged
  and stopped; the others go on.
- `scenes()` and `time()` of a scene's `ctx()` are those of *its* loop: use them from the
  scene, not from another thread.
- `engine.pool()` is the pool of worker threads (`Config::workers`, default: one per CPU),
  `ThreadPool::post(fn)` runs something on it. `engine.spawned()` counts the scenes still running.

The parts, if you need to read them: `SceneLoop` (a scene stack and its clock, driven by
whoever owns it), `TickDriver` (the pooled scenes), `Mailbox`, `StopSignal`.

The `Logger` can be used from any thread: a line is never cut by another one.

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
| `Engine.hpp` | The loop. `Config{mode, tickRate, maxCatchUp, maxFps, workers}`, `run()`, `stop()` (any thread), `step(seconds)` (one loop, for tests), `spawn<T>(policy, ...)`, `pool()`, SIGINT/SIGTERM handled. |
| `Scene.hpp`, `SceneManager.hpp` | Scenes and their stack. `Scene::setup()` gives a `SceneSetup` to functions that install systems (`installPhysics`). |
| `Message.hpp`, `Mailbox.hpp`, `SceneHandle.hpp` | Messages between scenes (see [Threads](#threads)). |
| `SceneLoop.hpp`, `TickDriver.hpp`, `ThreadPool.hpp`, `StopSignal.hpp` | What runs scenes on threads, and the worker threads (kronkpool). |
| `Module.hpp`, `Ref.hpp` | Modules and `Ref<T>` resources. |
| `Time.hpp`, `Stage.hpp`, `FixedTimestep.hpp` | The clock, the stages, and the accumulator (which knows nothing about clocks: it is tested by giving it frame durations). |
| `Math2D.hpp`, `Transform2D.hpp` | `Vec2`, `Rect` (y points down, angles are degrees, clockwise), `Transform2D`, `PreviousTransform2D`. |
| `Serializer.hpp` | `ByteWriter` / `ByteReader`: binary data, little-endian, the same on every machine. Everything read is checked (sizes announced by the data are verified before allocating). Versioned headers (`writeHeader`/`readHeader`), atomic file writes (`writeFile`). |
| `ConfigFile.hpp` | `key = value` files with `[sections]`, typed reads with fallbacks, alphabetical output. |
| `TileMap.hpp` | A level made of tiles, as data (see [Tilemaps](#tilemaps)). |
| `AssetManager.hpp` | Loads files once and shares them while somebody holds them. `loadAsync()` reads them on the workers (see [Loading in the background](#loading-in-the-background)); with a reloader, `reloadChanged()` reloads in place the files that changed (see [Hot reload](#hot-reload)). |
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
next input the player pushed. The file looks like this (a comment is a whole line that starts
with `#`: after a value, `#` is part of the value):

```
input.shoot = Space, Pad.A
input.up    = W, Pad.DPadUp, Pad.LeftY-
# empty: nothing bound to pause
input.pause =
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
numbered from 0, row after row. A `Sprite` with a `sheet` shows its cell number `frame`: the
texture and the part of it come from the sheet, and the size is that of a cell.

```cpp
auto sheet = std::make_shared<kuge::Spritesheet>();
sheet->texture = client.textures().load("coin.png");
sheet->frameWidth = 16;
sheet->frameHeight = 16;

kuge::Sprite sprite;
sprite.sheet = sheet;
sprite.frame = 3;                                   // out of range: the first cell
world.add<kuge::Sprite>(e, sprite);

world.add<kuge::Animator>(e, kuge::Animator::loop(sheet, 12.0f));   // every cell in turn, 12 a second
```

For more than one loop, animations are described in a text file, one clip per line:

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
animation is part of the simulation) and gives the `Sprite` the sheet and the frame that is
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

### Loading in the background

Reading and decoding a picture takes time, and stalls a frame if it is done in the middle of
the game. `client.textures().loadAsync(path)` gives a **ticket** at once and reads the file
on a worker thread; the texture itself is made by the main thread (a texture belongs to the
renderer's thread) at the start of the next loops:

```cpp
auto ticket = client.textures().loadAsync("boss.png");
...
if (auto texture = ticket->asset()) { sprite.texture = texture; }   // Ready
else if (ticket->state() == Ticket::State::Failed) { log(ticket->error()); }
```

Asking twice for a file that is on its way shares the load, and a file that is already
loaded gives a ticket that is ready at once. It is generic: `AssetManager<T>::enableAsync(
pool, prepare, finish)` takes the two halves (`prepare`: file to data, on a worker; `finish`:
data to asset, on the thread that calls `pump()`). The client does it for textures and pumps
at the start of each loop. A ticket holds its asset: let go of the ticket once you have it.

### Hot reload

While you work on a game, you do not want to restart it for each change of a picture or a
sound. Ask the client to watch its assets:

```cpp
engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend({}), kuge::ClientConfig{.watchAssets = 0.5});
```

Every 0.5 second (0, the default, means never) the textures, sounds and musics that were
loaded with `client.textures()` / `sounds()` / `musics()` are compared with their files (by
date and size). A file that changed is **reloaded in place**: the `Texture` object stays the
same, so every sprite that holds it draws the new picture on the next frame, with nothing
to do (its size may change too). `client.reloadAssets()` does the same check at once.

- A file that cannot be read (a picture that is half written, a wrong format) is logged and
  the asset **keeps its old content**; the file is only tried again when it changes once more.
- A file that is missing is left alone (editors often delete, then write).
- An asset that nobody holds any more is not reloaded: the next `load()` reads the file.
- Fonts and files you read yourself (tilemaps, `.anim`) are not watched.

The mechanism is generic: give a reloader (a function that reads a file *into* an existing
asset) to any `AssetManager<T>`, and call `reloadChanged()` from the thread that uses it.

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

## Network module (`kuge-net`)

Depends on the core only (never on the client): a server links it without SDL.

```
        Net (a scene's endpoints, polled in the Network stage)
         |
      Endpoint  -- typed messages (KUGE_MESSAGE), handshake, channels, keep-alive, timeouts
         |
      ITransport -- whole packets: TCP (framed), UDP, or in-memory Loopback
```

### Messages

```cpp
struct PlayerMoved {
    KUGE_MESSAGE(PlayerMoved, id, position, heading)   // the fields that go on the wire, in this order
    std::uint32_t id = 0;
    kuge::Vec2    position;
    float         heading = 0;
};
```

`KUGE_MESSAGE` gives the struct an id (a hash of its name: the same on every machine) and
says how to write and read it through the core's `Serializer` (little-endian, checked reads).
Fields can be numbers, bools, enums, strings, vectors, arrays, optionals, `Vec2`, `Rect`, and
other messages (up to 24 fields). Both sides need the same name and fields. The struct must be
a type of a namespace, not a local class.

### Endpoints

```cpp
kuge::net::Endpoint server(transport, kuge::net::Role::Server);
server.onConnected([](kuge::net::ConnectionId id) { ... });
server.onDisconnected([](kuge::net::ConnectionId id, kuge::net::DisconnectReason why) { ... });
server.on<PlayerMoved>([&](kuge::net::ConnectionId from, const PlayerMoved& moved) { ... });

server.send(id, PlayerMoved{...}, kuge::net::Channel::Unreliable);
server.broadcast(Chat{...}, kuge::net::Channel::Reliable, /*except*/ id);
server.poll();                 // once per tick: reads, sends what is due, calls the handlers
```

- **Channels.** `Unreliable` is sent once: it may be lost, arrive twice or out of order (positions,
  inputs). `Reliable` arrives **once and in order**, however many times it has to be sent: each
  message is numbered, acknowledged (the acks ride on the traffic, with a bit mask for what came out
  of order), and sent again after a wait that follows the round trip time and doubles at each miss.
  A window (256 in flight) and a queue (1024) bound the memory: `send` returns `false` when they
  are full, instead of growing. Over TCP nothing is ever sent twice.
- **Handshake.** A client sends `Connect` until the server answers `Accept`; "connected" therefore
  means that the other side answered, even over UDP where a socket knows nothing. A server that is
  full, or has another `protocol` version, refuses (`DisconnectReason::Refused`).
- **Keep-alive and timeouts.** A silent connection sends something every second, and a peer that says
  nothing for 10 seconds is gone (`Timeout`). Leaving (`disconnect`, or destroying the endpoint) tells
  the peer at once. All the delays are in `EndpointConfig`, with a clock you can replace.
- **Handlers** run at the end of `poll()`, when the endpoint is in order: they may send, broadcast,
  disconnect, and add or remove endpoints.
- **Bad data** (junk packets, cut messages, unknown types) is dropped and counted in `stats()`, never
  fatal. A message is at most 8192 bytes with its header.
- One thread uses an endpoint at a time (the one that polls it).

### Transports

| Transport | Made with | Notes |
|---|---|---|
| TCP | `makeTcpServer(port)`, `makeTcpClient(host, port)` | The stream is cut into whole packets (4-byte length, then the packet), whatever way TCP cuts or glues the bytes. A peer that announces a packet larger than 8192 is dropped. |
| UDP | `makeUdpServer(port)`, `makeUdpClient(host, port)` | A packet is a datagram. |
| Loopback | `LoopbackNetwork::listen(name)`, `connect(name)` | In memory, by name, **thread-safe**: a client and a server in the same process, on different threads (a player who hosts the match). |

TCP and UDP go through kronknet, IPv4 only ("localhost" or a dotted address). A server that cannot
bind throws; a client that cannot reach its server does not throw, its endpoint reports a
disconnection.

`LoopbackNetwork` can misbehave on purpose: `Conditions{.loss, .duplicate, .latency, .jitter, .seed}`
lose, repeat, delay and reorder packets (the same seed loses the same ones), and its clock can be
replaced, so a test can make ten seconds pass in no time. The reliable channel is tested over
50 % loss and repeats with it.

### In a scene

```cpp
kuge::net::installNet(setup());                                   // a Net resource, polled in the Network stage
auto& net = world().getResource<kuge::net::Net>();

auto& server = net.listen(kuge::net::Protocol::Udp, 4242);        // or net.listen("room", loopbackNetwork)
auto& client = net.connect(kuge::net::Protocol::Tcp, "127.0.0.1", 4242);
```

A scene's endpoints are polled by its own thread and destroyed with the scene (their peers are
told). Two scenes on two threads talk through a network address, or through a loopback: a
client on the main thread and a room on its own thread is the "player who hosts" case.
kronknet is not thread-safe (a counter is shared by the whole process), so every call into it goes
through one lock.

## Server module (`kuge-server`)

A game server with no window and no drawing code: it needs the core and the network only (a
test checks that it cannot include the client).

```
 client                        lobby (well known address)                room (its own address)
   | -- JoinRoom ------------->  |                                          |
   | <-- RoomAssigned(address, token)                                       |
   | ------------------------- connect ----------------------------------> |
   | ------------------------- Hello(token) ----------------------------->  |
   | <------------------------ Welcome(networkId) -------------------------- |
   |                          ... the game ...                              |
   | <------------------------ RoomClosed --------------------------------- |
   | (back in the lobby, which the client never left)
```

```cpp
class DeathmatchRoom : public kuge::server::RoomScene
{
    public:
        using RoomScene::RoomScene;
    protected:
        void onRoomEnter() override
        {
            on<Input>([this](const Player& who, const Input& input) { ... });      // messages of the game, from players only
            addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Simulate>());
        }
        void onPlayerJoined(const Player& p) override { send(p.networkId, Snapshot{...}); }
        void onPlayerLeft(const Player& p, kuge::net::DisconnectReason) override { ... }
};

int main()
{
    kuge::server::GameServer server({.lobbyPort = 4242});
    server.addRoomType<DeathmatchRoom>("deathmatch", {.maxPlayers = 8});
    return server.run();          // until Ctrl+C: every room is closed properly
}
```

The client side is in `kuge-net` (a client links it without the rooms): a `MatchmakingClient`
connects to the lobby, `join("deathmatch", "Ana")` asks for a room, and `onJoined` gives the
`Endpoint` of the room, on which the game registers its messages.

- **Lobby.** A scene on its own thread. Clients connect to it (TCP by default: the connection
  stays for the whole session, and losing it takes the player out of the room). It finds the room
  of the asked kind that has a free place (the fullest first), or makes one, up to `maxRooms`. A room is
  a scene spawned on its own thread (or on the workers: `RoomTypeConfig::policy`), on a UDP port taken
  from a range, or at a name of a loopback. It refuses with `JoinError` (unknown kind, full, room could
  not start...).
- **Choosing a room.** `join()` lets the lobby choose. A player can also choose: `requestRooms("deathmatch")`
  asks for the **public** rooms (answer to `onRoomList`: each has an `id`, a `name`, `players` and
  `maxPlayers`; at most 64, `total` says how many there are), `createRoom(kind, "Les copains", "Ana")` opens a
  room that its creator names and puts the creator in it, and `joinRoom(id, "Les copains", "Ben")` takes the
  room that has this id **and** this name. With `createRoom(..., true)` the room is **private**: it is in no
  list and the automatic matchmaking never fills it, so only someone who knows its name *and* its id gets in
  (a wrong name, a wrong id and a room that does not exist all answer `UnknownRoom`, so it cannot be guessed
  at that way: but ids go 1, 2, 3..., so the name is the only secret, and there is no password). A name is 1 to 32 bytes
  of UTF-8 with no control character (`validRoomName()`, and `trimRoomName()` drops the blanks at its ends).
  A room that the lobby makes for `join()` is called `"<kind> #<id>"`, and a room sees its name and whether it is
  private in `init().roomName` and `init().isPrivate`. The client stays in the lobby all along: it can look at
  the list, leave a room and join another.
- **Tokens.** The lobby gives the client the address of the room and a random 64-bit token, and tells
  the room to expect it. The token opens the door **once** and expires after `tokenTtl` (10 s): a
  client that never comes loses its place. A client that connects to a room and does not say `Hello` in
  `helloTimeout` is dropped; an unknown or used token is `Rejected`. Nothing a client sends to a room
  before its `Hello` is given to the game.
- **Network ids.** A welcomed player is a `Player` with a `networkId` (1, 2, 3... in this room, never
  reused): how the game names it. `playerId` is server-wide.
- **The end.** `finish()` (or an idle room: `idleTimeout`, or the server stopping) tells the players
  after `closeDelay` (the room keeps running, so the last snapshots, with what the game did as it ended, leave
  before "room closed": a client lets go of the room when it hears it), gives the last messages a moment to
  leave (`linger`), and leaves the scene. The lobby gets the place and
  the port back, and the clients are in the lobby again: the lobby learns that the game is over *before* the
  players do, so that a client that asks for another game at once is not told "already in a room".
- **Brutal losses.** A client whose cable is cut is found by the timeouts of the endpoints (or by the lobby
  when its connection dies), and its place is freed.
- **Headless.** `GameServer` uses an engine in `Headless` mode.

`ServerConfig` says how it is reached: `Transport::Sockets` (a lobby on `lobbyPort` over TCP or UDP, the rooms on
UDP ports from `roomPortFirst`, `roomAddress` being what clients are told to use) or `Transport::Loopback` (by
name, in the process, for tests or a game that hosts its own server). `stats()` counts rooms, players and joins
from any thread.

`example/chat` is a real one: `kuge_chat_server [port]` and `kuge_chat_client <name> [host] [port]` (a chat room
of four, in the terminal).

## Replication module (`kuge-replication`)

Depends on the network and the physics (not on the client, not on the server: a room and a client both
use it). It answers "how does what happens in the room show on the screens of the players": the room says
which entities exist, the clients get a copy that follows, and the player's own entity answers at once.

```
   room (server)                                                    client
   World ── ReplicationServer ── snapshots (unreliable) ──▶ ReplicationClient ── World (a copy)
             ▲   what changed since the last one that                │ interpolated a little in the past
             │   the client acknowledged                             ▼
   InputServer ◀── inputs (numbered, redundant) ── Prediction ── the player's entity, at once
                      snapshot + "last input applied" ──▶ corrects it, replays the inputs the server has not seen
```

### What is replicated

A `ReplicationRegistry`, built by one function that the room and the clients both call, lists the components
that travel, and how:

```cpp
kuge::replication::ReplicationRegistry registry;

registerTransform2D(registry, Replicate::Interpolated, /*predicted*/ true);
registerBody(registry, Replicate::OnChange, /*predicted*/ true);
registry.component<Health>("Health", Replicate::OnChange,
    [](kuge::ByteWriter& out, const Health& h) { out.write(h.points); },
    [](kuge::ByteReader& in) { return Health{in.read<int>()}; });
```

- `Replicate::Interpolated`: changes all the time (positions). Needs a `lerp`. The client draws it a moment
  in the past, between the last two values it got.
- `Replicate::OnChange`: sent when it changes, applied at once (health, score).
- `Replicate::OnSpawn`: sent once, when the client learns of the entity (team, colour).
- The order of the registrations is the number of a component on the wire (64 at most). A hash of the list
  travels with each snapshot, and a client ignores those of a server whose list differs.

### The room's side

```cpp
ReplicationServer replication(world(), registry, endpoint());
InputServer<Steer> inputs(endpoint());

// when a player joins:
const NetworkId id = replication.track(ship, ShipType, /*owner*/ player.networkId);
replication.addClient(player.connection);
inputs.addClient(player.connection);

// each tick:
for (const auto& applied : inputs.collect()) {                 // one input per player, in order
    steer(world, shipOf(applied.connection), applied.input);   // the function that the client predicts with, too
    replication.setInputAck(applied.connection, applied.sequence);
}
/* ... the simulation ... */
replication.update(time.tick);                                 // Replication stage
```

- **Snapshots** are unreliable and computed against the last one that the client **acknowledged**: a lost
  snapshot only makes the next one a little bigger, and a client that has nothing (new, or too far behind) is
  sent everything. Only what changed is in them (an entity that appeared, a component that changed, an entity
  that went); the same world always gives the same bytes. A big snapshot comes in several packets.
- The client builds each snapshot **from the one it acknowledged** (it keeps the last 32), not from the last
  one it applied, so an entity that appeared and went between two acknowledged snapshots, or a value that
  changed and came back, cannot be left wrong.
- `setFilter` lets the room say what a client may see (interest management).
- `track` / `untrack` (or removing the entity from the World) is all it takes: an entity that is not tracked
  any more goes from the clients at the next snapshot.

### The client's side

```cpp
ReplicationClient replication(world, registry);
replication.onSpawn(ShipType, [](kw::World& world, kw::Entity e, const SpawnInfo&) { world.add<Sprite>(e, ...); });
replication.setLocalPlayer(welcome.networkId);
replication.attach(room);                     // the Endpoint from MatchmakingClient::onJoined
...
replication.update(frameSeconds);             // each frame
```

Entities appear in the World with a `Replicated` (network id, type, owner) and their components, and the prefab
adds what the server does not send (the sprite). `entity(networkId)` finds one. Interpolated components are
placed at `interpolationDelay` (0.1 s) behind the newest snapshot: smooth at any frame rate, though snapshots
come 30 times a second and some are lost; past its last value an entity waits, nothing is invented.

### Prediction

```cpp
PredictionConfig<Steer> config;
config.build = [](kw::World& world) { buildLevel(world); return buildPlayer(world); };   // the same as the room's
config.apply = &steerPlayer;                                                             // input -> entity
config.step  = [](kw::World& world, double dt) { world.getResource<kuge::Physics2D>().step(world, float(dt)); };

Prediction<Steer> prediction(config, world, registry, replication, room);
...
prediction.tick(steer);                       // each fixed tick, with what the player is doing
```

- **Each tick** the input is numbered, simulated on a **private world** that holds only the level and the
  player, and copied to the entity that the game draws: the player answers at once, walls included. The last
  four inputs go to the server in each packet, so a lost one is usually in the next.
- **Each snapshot** carries the state of the player's entity and the number of the last input that state
  includes. The private entity is put in that state, the inputs after it are simulated again, and that is the
  new prediction. If the room simulates what the client simulated (same code, same inputs, deterministic
  physics), **nothing changes**: with 100 ms of round trip and 10 % loss the prediction was corrected 3 times in
  450 snapshots.
- When the server does contradict the client (something it cannot know: another player, a wall that only the
  server has), the entity is **not teleported**: it keeps being drawn where it was and slides to the new place
  over `smoothing` seconds. Only a difference of more than `snapDistance` (a respawn) is shown at once.
- The components to predict are the ones registered `predicted`; the entity's other components are the
  server's (`OnChange`), and the player's own entity is never interpolated.
- The room repeats the last input when the next one is not there, drops one that comes after its turn, and
  can hold a few inputs back (`InputServerConfig::jitter`) to absorb jitter.

Nothing in it reads a clock: it is driven by the ticks and the frame durations that the caller gives, so it is
tested with a network, and a time, of its own: the same network and the same inputs give the same positions,
tick for tick, bit for bit.

## R-Type: one game, three ways

`example/rtype` is the check that the modules make any game, and that the separation of the client and the
server is real. It is one game, in one set of files, run three ways, and none of them changes the engine:

```
example/rtype/
  common/   what both sides share: the vocabulary (Steer, Health...), what is replicated, the arena, the rules
  server/   RTypeRoom (a RoomScene) and main(): no window, links kuge-server
  client/   RTypeScene (a ClientScene) and main(): a window, links kuge-client
  host/     main(): both, in one process
```

| Program | What it is | How it reaches the room |
|---|---|---|
| `kuge_rtype_server [port]` | a **dedicated server**: lobby and rooms, no window (`Headless`) | clients come over TCP (lobby) and UDP (rooms) |
| `kuge_rtype_client [--host H --port P]` | the **client**: window, keys, drawing | sockets |
| `kuge_rtype_host` | a **player who hosts**: the client scene on the main thread, the room on a thread of its own | a loopback network of the process |
| `platformer` (the other example) | **solo**: core, client, physics, no network at all | none |

- The **rules** (`Rules.cpp`) are plain functions on a World: bullets fly, enemies come and wave, things hit, ships
  die, the game is over. They are tested alone, and the room only calls them.
- The **room** applies the inputs (`InputServer`), moves the ships (the same `steerShip` and physics as the
  client's prediction), runs the rules, and shows the World to the clients (`ReplicationServer`). It knows
  nothing of a screen.
- The **client scene** joins with a `MatchmakingClient`, predicts its own ship (a private world with the arena and
  the ship), draws the others between the snapshots, and puts a `Sprite` on each entity that appears (a prefab):
  the server never sends how things look. It asks for another game when one ends.
- The **host** is `RTypeScene` and `RTypeRoom` again: only `ClientOptions::sockets = false` changes, and a server
  thread is started. A game that lets a player host the match is that, and nothing more.

`ctest` plays it: the rules alone (a bullet kills, an enemy hits, the same seed gives the same game); a host with
two pilots (they see each other, the bullets and the enemies, a game ends and the next one starts, pilots come and
go); a dedicated server over real sockets, with four pilots; the server stopping with pilots in a game; and
`rtype_smoke`, which starts the real programs (server, then client, then host) on SDL's dummy screen and checks
the picture they leave.

**What building it showed** (the point of the exercise: what the modules did not do well enough). Three
defects, all fixed in the modules, none in the game:

1. *`MatchmakingClient` did not say that a room was lost when the lobby was lost.* A game that holds the room's
   endpoint (a prediction, a replication client) kept a pointer to something already destroyed. `onRoomClosed` now
   fires for every way a room can end, except the ones the caller asked for.
2. *A socket transport read one datagram per poll* (that is all kronknet does in one call). A room polled 60 times
   a second heard 60 packets a second, and a client sending an input each tick plus acknowledgements filled the
   socket faster than it was emptied: inputs were applied later and later. A poll now reads until the socket is
   empty (at most 512 packets), tested with a burst of 200 datagrams in one poll.
3. *An entity owned by a player was always taken for the player's predicted entity*, so its bullets were given to
   the prediction and never moved. A client now says which type it predicts (`predictType`).

It also shows a rule of thumb: the client and the room must agree on the clock. A scripted client that slept
"16 ms" instead of 1/60 s ran 4 % faster than its room, and its inputs piled up; the room keeps two inputs of
margin (`InputServerConfig::jitter`), and the scripted runs keep an absolute schedule.

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
cmake -S . -B build -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON   # the smoke tests and R-Type's run the examples
cmake --build build -j
ctest --test-dir build --output-on-failure
```

| Program | Covers |
|---|---|
| `kuge_tests` | core: loop, scenes, modules, serializer, config, tilemaps, assets (reload, background), saves, snapshots, messages, and scenes on threads |
| `kuge_net_tests` | net: messages on the wire, the reliable channel alone, the loopback, endpoints (handshake, timeouts, refusals, junk), a lossy network, real TCP and UDP sockets, scenes on threads that talk, endpoints ended by their own handlers |
| `kuge_net_fault_tests` | net: UDP when a socket's send buffer is full (`send`/`sendto` are wrapped at link time to say "try again"): nothing is glued, nothing is lost |
| `kuge_server_tests` | server: joining, rooms filling and multiplying, tokens (wrong, used, expired), silent peers, leaving, the lobby lost, a cut cable, the end of a game and playing again at once, 24 clients, pooled rooms, stopping with players, real sockets, a room that cannot open |
| `kuge_replication_tests` | replication: spawns, changes and removals over a network that loses and reorders, big snapshots, interpolation (smooth, still then moving, rotation), prediction (same inputs same positions, immediate answer, walls, 100 ms latency with loss, a wall the client cannot see, snaps, replay of a whole run), the inputs on the server, and the whole stack through a room with two players |
| `kuge_rtype_tests` | R-Type: the rules, a host (server and client in one process), a dedicated server over sockets with four pilots, the server stopping with players |
| `kuge_logger_tests`, `kuge_logger_init_tests` | logger: whole lines from many threads, first use from many threads |
| `kuge_client_tests` | client: inputs, sprites, tiles, animations, audio, text, the UI (layout, focus, mouse, drawing), the SDL backend (on SDL's dummy screen and audio, reading real pixels), and the game logic of Pong and of the platformer |
| `kuge_physics_tests` | physics: shapes, broad phase, scenarios, a property test, bit-for-bit replay |
| `boundary_*` | a module cannot include what it does not link |
| `rtype_smoke` | the real R-Type programs (server, client, host) play a scripted game and leave a picture that is checked |
| `pong_smoke`, `platformer_smoke` | the real binaries play a scripted game and leave a picture that is checked |

Things that are worth knowing:

- **kronklab** (the test library of kronk*) always exits with 0, so `ctest` reads its report
  line instead (`cmake/RunKronklab.cmake`). Test names are limited to 31 characters.
- Tests that protect against a specific mistake were checked by **breaking the protection on
  purpose** and seeing the test fail (sub-stepping and the contact skin in the physics, the
  seam-free tile edges in the drawing).
- Sanitizers: `cmake -S . -B build-asan -DBUILD_TESTS=ON -DKUGE_SANITIZE=address` (also UBSan) and
  `-DKUGE_SANITIZE=thread` (on a recent kernel, run the tests with `setarch "$(uname -m)" -R`).
  Everything is clean under both, threads included (the thread tests are the reason TSan matters:
  16 scenes with 10 000 ticks each, a ring of scenes passing a message, a hundred starts and stops).

## Project layout

```
CMakeLists.txt        options, pinned dependencies, modules
cmake/                KugeModule.cmake (kuge_module, kuge_vendor, kuge_add_test), test helpers
modules/logger/       kuge-logger
modules/core/         kuge-core
modules/physics/      kuge-physics
modules/client/       kuge-client (input/, render/, animation/, audio/, ui/, backend/, sdl/)
modules/net/          kuge-net (Wire, Reliable, Endpoint, Loopback, SocketTransport, Net, Matchmaking)
modules/server/       kuge-server (GameServer, the lobby, RoomScene)
modules/replication/  kuge-replication (registry, snapshots, interpolation, inputs, prediction)
example/              the headless example, pong/, platformer/, chat/ and rtype/ (common, server, client, host)
tests/                one folder per module, and the boundary tests
vendor/               submodules: kronkworld, kronkpool, kronknet, kronk3d
```

Each module is a static library (`kuge-core`...) built from an object library, which the
optional `kuge.so` reuses. To add one: a `CMakeLists.txt` with
`kuge_module(name DEPENDS kuge-core ...)` and its sources in `src/`.
