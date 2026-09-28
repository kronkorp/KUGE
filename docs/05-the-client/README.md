# 05 The client

`kuge-client` turns an engine into something you can see and play: a window, inputs, 2D drawing, sound,
text, menus, tilemaps and animations. It needs SDL2, but **the game does not talk to SDL**: it talks to small
interfaces, so a test (or another library) can take SDL's place.

## Adding it

```cpp
kuge::Engine engine({.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend({.title = "My game", .width = 960, .height = 540}));
```

The client module reads the window and inputs at the start of each loop, and shows the picture at the end.
It gives each scene these resources: `Ref<IWindow>`, `Ref<IRenderer2D>`, `Ref<InputMap>`,
`Ref<AssetManager<Texture>>`, `Ref<Audio>` with sound and music managers, `Ref<TextRenderer>`, `ActionState`,
`Camera2D` and more.

## Backends

The client depends on interfaces gathered in a `Backend`: `IWindow`, `IInputSource`, `IRenderer2D`, and
optionally `IAudio` and `IFontLoader`.

- `makeSdlBackend(WindowConfig)` is the default: SDL2 for the window, keyboard, mouse and gamepads, drawing on
  the GPU or the CPU; SDL_mixer for sound; SDL_ttf for fonts. If there is no audio device, the game runs
  silently and says so in the log.
- `makeDummyBackend()` draws nothing and **records** what it was asked to draw and play. It is how the games are
  tested with no screen: the R-Type tests run whole clients on it.

## A scene that draws

A scene that draws derives from `ClientScene` and calls `installClientSystems()` first in `onEnter()`. That
adds, in this order:

- Fixed, `Input`: `SampleInput` (the player's actions for this tick) and `SnapshotTransforms` (remember where
  things were, for smooth drawing);
- Fixed, `Late`: `AnimateSprites`;
- Frame, `Render`: `SpriteRender` (sprites and tilemaps).

```cpp
class Game : public kuge::ClientScene
{
    public:
        void onEnter() override
        {
            installClientSystems();
            auto& camera = world().getResource<kuge::Camera2D>();

            camera.position = {320, 180};          // the point of the world at the centre of the screen
            camera.zoom = 1.5f;
        }
};
```

## Inputs: actions, not keys

The game never sees a key: it sees **actions**, defined as an enum (up to 64).

```cpp
enum class Action : std::uint8_t { Left, Right, Fire };

auto& input = client.input();

input.declare(Action::Fire, "fire");                  // its name in the keybinds file
input.bind(Action::Fire, kuge::Key::Space);
input.bind(Action::Fire, kuge::GamepadButton::A);     // several inputs, one action
input.bind(Action::Left, kuge::Binding::axis(kuge::GamepadAxis::LeftX, -1));
input.loadBindings("keybinds.cfg");                   // what the player changed wins
```

Read them in a fixed-tick system:

```cpp
const auto& actions = world.getResource<kuge::ActionState>();

if (actions.isDown(Action::Fire))    { ... }     // held during this tick
if (actions.wasPressed(Action::Fire)) { ... }    // went down since the last tick (never missed, even for a very short press)
```

The player can rebind everything: `input.saveBindings("keybinds.cfg")` writes
`input.fire = Space, Pad.A`, and `startCapture()` / `takeCaptured()` implement "press the key you want".

## Sprites, camera, layers

A drawable entity has a `Transform2D` and a `Sprite`:

```cpp
kuge::Sprite sprite;

sprite.texture = client.textures().load("hero.png");   // shared while somebody holds it
sprite.source = {0, 0, 16, 16};                        // a part of the picture (width 0: all of it)
sprite.size = {16, 16};                                // in the world
sprite.layer = 3;                                      // higher layers are drawn over
sprite.tint = kuge::Color{255, 255, 255, 255};
world().add<kuge::Sprite>(entity, sprite);
```

**A sprite with no texture is a plain rectangle of the colour of its tint.** R-Type is drawn with nothing
else: no art files at all. It is a good way to start.

Drawing order is `(layer, z, texture, entity)`, so it is stable. The `Camera2D` resource says which point of
the world is at the centre of the screen and how much it is zoomed.

If an entity also has a `PreviousTransform2D` (the client keeps it up to date), it is drawn **between** its last
two ticks by `Time::alpha`: smooth on any refresh rate.

## Assets

`client.textures().load(path)` reads a file once and shares the object while someone holds it (the same for
`sounds()` and `musics()`). Two extras:

- **Hot reload**: `ClientConfig{.watchAssets = 0.5}` compares the files of loaded assets every 0.5 s and reloads
  a changed one **in place**: every sprite that holds the texture draws the new picture on the next frame. A
  half-written file is logged and the asset keeps its old content.
- **Loading in the background**: `loadAsync` (see [04](../04-threads-and-messages/README.md)).

## Sound

`Ref<Audio>` (or `client.audio()`):

```cpp
auto boom = client.sounds().load("boom.wav");

audio.play(*boom, {.volume = 0.8f});                         // {.volume, .pan, .loop}
audio.playAt(*boom, enemyPosition, {.position = cameraPos}); // louder when near, panned by where it is
audio.setBusVolume(kuge::Bus::Music, 0.4f);                  // Effects and Music, under the master volume
audio.playMusic(*theme, {.fadeIn = 2.0f});
```

## Text and UI

`client.loadFont("font.ttf", 14)` gives a font; `Ref<TextRenderer>` draws text in screen pixels. Menus, HUD
and options are **entities in screen pixels** made of `UiNode` (a rectangle placed by an anchor or a stack),
`UiPanel`, `UiLabel` and `UiButton`. `installUi(setup(), actions)` adds the systems: layout, focus navigation
with the keyboard or gamepad (nearest neighbour), mouse hover and click, and drawing. The platformer's
`MenuScene` and `PauseScene` are complete examples.

## Tilemaps and animations

- A **`TileMap`** (in the core, so a server can load one to know where the walls are) is a grid of layers of
  tile numbers with a list of solid tiles, with a small text format. The client draws it (culled, without seams)
  with a `Spritesheet` (a picture cut in equal cells).
- An **`Animator`** plays clips of a spritesheet (`.anim` text files: frames, fps, loop, cues that fire events at
  a given frame). `AnimateSprites` runs in the Fixed schedule so animations are as reproducible as the rest.

## Testing a client without a screen

```cpp
auto dummy = kuge::makeDummyBackend({960.0f, 540.0f});
kuge::Engine engine({.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
auto& client = engine.addModule<kuge::ClientModule>(std::move(dummy.backend));

client.input().handle(kuge::KeyEvent{kuge::Key::Space, true});   // the player presses fire
engine.step(1.0 / 60.0);
// dummy.renderer->lastFrame() lists what was drawn
```

This is how `tests/client` and `tests/rtype` play games: real scenes, real systems, no window.

## Scripted runs

Every example with a window accepts `--frames N --screenshot f.ppm`: a scripted player plays N frames and a
picture of the last one is written (PPM, which any viewer reads). With `SDL_VIDEODRIVER=dummy` this runs on a
machine with no display.

Next: [06 Physics](../06-physics/README.md).
