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

For a cell of a spritesheet, give the sprite the sheet instead: `sprite.sheet = sheet; sprite.frame = 3;`. The
texture and the part of it then come from the sheet.

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

audio.play(*boom, {.volume = 0.8f});                         // {.volume, .pan, .loop, .lowpass, .bus, .priority}
audio.playAt(*boom, enemyPosition, {.position = cameraPos}); // louder when near, panned by where it is
audio.setBusVolume(kuge::Bus::Music, 0.4f);                  // Effects, Music and Dialogue, under the master volume
audio.playMusic(*theme, {.fadeIn = 2.0f});
```

The volume heard is the master volume, times the one of the **bus**, times the one of the sound. Three buses, three sliders
for the player: `Effects`, `Music` and `Dialogue` (spoken lines). A voice is on the bus of its options (`{.bus = kuge::Bus::Dialogue}`),
so `playAt` and the 3D `playAt` take it too: a character who speaks from the left is heard on the left, at the volume of the dialogue.

**A line must not be lost to a flood of effects.** The mixer has 32 voices; `audio.reserveVoices(kuge::Bus::Dialogue, 2)` (once, at
the start) keeps two of them for the dialogue. Effects never take them; when both are busy a third line is not played (an empty
`Voice`), unless it has a higher `priority` than one that plays, which it replaces.

**Priority.** When all the voices are busy, a new sound takes the place of a playing one of lower priority, or of the same priority
that is quieter, never of a higher one: a shot of the player (`priority = 5`) is not lost to a far footstep, and with 40 sounds at
once the nearest are kept. The default priority is 0, so a game that sets none still keeps the loudest.

**Ducking.** `audio.setDucking(0.3f, 0.2f)`: while a line of the dialogue plays, the music is multiplied by 0.3, getting there and coming
back over 0.2 s. The slider of the player is not touched. The client calls `audio.update(seconds)` once per loop; a test calls it itself.

**Streams.** Samples that arrive while they are played, a voice from the network or a synthesiser: one stream per speaker.

```cpp
auto speaker = audio.createStream(48000, 1);                 // 1 or 2 channels, 16 bits, interleaved
speaker->push(frames);                                       // as they arrive (it holds 5 seconds)
audio.play(*speaker, {.bus = kuge::Bus::Dialogue});          // once a few frames wait: a jitter buffer
```

It plays what it has, resampled to the mixer's rate, and the voice **ends when nothing is left** (play it again after pushing more:
that is the "stop when frames stop" of a voice chat). Recording a microphone, a codec and relaying voice frames over the network are not
there yet (ticket 014).

## Text and UI

`client.loadFont("font.ttf", 14)` gives a font; `Ref<TextRenderer>` draws text in screen pixels. Menus, HUD
and options are **entities in screen pixels** made of `UiNode` (a rectangle placed by an anchor or a stack),
`UiPanel`, `UiLabel`, `UiButton`, `UiTextField`, `UiProgress` (a bar for health or XP, filled for `value / max`) `UiCheckbox` (on or off:
`checked`, and `Changed` in `UiEvents` when it is flipped) and `UiSlider` (a value in a range, for the volume:
left / right, or the mouse; `Changed` when it moves). A long list is a node with a size, a `UiStack` and a
`UiScroll`: the mouse wheel scrolls it, the keyboard focus brings its child into view, and what is outside is cut
(`IRenderer2D::setClip`). The wheel is also an input: `Binding::wheel(1)` is "Wheel.Up".
`UiImage` puts a texture (an icon, a portrait) in the rectangle of a node.

**Any resolution.** Build the UI for 1280 x 720 and set `UiScale::value = uiScaleFor(window.size())` when the window
changes: the layout multiplies every pixel number by it (the components keep theirs) and the text is drawn with the
same font at `value` times its size (`IFont::scaled()`), so it stays sharp. `installUi(setup(), actions)` adds the systems: layout, focus
navigation with the keyboard or gamepad (nearest neighbour), mouse hover and click, and drawing. The platformer's
`MenuScene` and `PauseScene` are complete examples.

**Typing.** What the player types is a `TextEvent` (UTF-8), apart from the keys: a key is a place on the keyboard,
a letter is what the layout and the OS make of it. A `UiTextField` is a line of text that takes it: click it (or
reach it with the focus), type, edit with Backspace, Delete, the arrows, Home and End, and read `Changed` and
`Submitted` (Enter) in `UiEvents`. While a field has the focus the actions that move it (up, down, left, right,
accept) are left alone, since a letter that your game bound to "up" is a letter there: **Tab** leaves the field.
For your own input, `InputMap::takeTyped()` and `takeEditKeys()` give the text and the editing keys since the last
call. **Ctrl+V** (Cmd+V on macOS) pastes the first line of the clipboard, **Ctrl+C** copies the whole text and
**Ctrl+X** cuts it; a game reaches the clipboard with `Ref<IWindow>` (`clipboardText()`, `setClipboardText()`).
There is no selection or multi-line text yet. The details are in the README (User interface).

## Tilemaps and animations

- A **`TileMap`** (in the core, so a server can load one to know where the walls are) is a grid of layers of
  tile numbers with a list of solid tiles, with a small text format. The client draws it (culled, without seams)
  with a `Spritesheet` (a picture cut in equal cells).
- An **`Animator`** plays clips of a spritesheet (`.anim` text files: frames, fps, loop, cues that fire events at
  a given frame). `AnimateSprites` runs in the Fixed schedule so animations are as reproducible as the rest.
  `Animator::loop(sheet, fps)` plays every cell of a sheet in turn, without a file.

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
