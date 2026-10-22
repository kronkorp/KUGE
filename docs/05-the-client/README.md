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

**Axes** have a value instead of a state: a mouse look reads `input.axisValue(Action::LookX)`, the sum of how far the
mouse moved during the frame along the bound axes (`"Mouse.X"`, `"Mouse.Y"`) and of how far a stick is pushed
(`"Pad.RightX+"`, towards that way only; a dead zone of 0.15 hides the drift). Each axis binding has a
**sensitivity**, a negative one inverts, and it is saved with the binding: `input.look_x = Mouse.X*2.5, Pad.RightX+*3`.
No acceleration, no smoothing. Hide the cursor and keep the mouse in the window with
`world().getResource<kuge::Ref<kuge::IInputSource>>()->setRelativeMouse(true)`; the window gives it back when it loses the focus.

`ViewAngles` (`input/ViewAngles.hpp`) is the camera of a first or third person game: turn it **each frame**, so it
never feels late, and sample it at each tick for the input, as absolute angles (a lost input then loses nothing of the aim):

```cpp
view.turn(input.axisValue(Action::LookX), input.axisValue(Action::LookY));   // each frame; pitch stays within 89 degrees
message.yaw = kuge::packAngle(view.yaw);                                       // each tick: 16 bits, 0.0055 degrees
```

Keys are named after their **place** on the keyboard, not the letter printed on it (`Key::W` is the key where a QWERTY keyboard has W, on any layout): a shooter's WASD works for everybody, and the text typed in a field is a separate event.

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

### How much of the world is shown

With nothing else, what the player sees is the window divided by the zoom: a bigger window shows **more of the world**.
A game that wants the same area at any resolution gives the camera a **logical size**:

```cpp
auto& camera = world().getResource<kuge::Camera2D>();

camera.logicalSize = {640, 360};                          // the screen the game is made for, at a zoom of 1
camera.fit = kuge::Camera2D::Fit::IntegerLetterbox;
```

| `fit` | In a window of another shape |
|---|---|
| `Stretch` | Fills it, the pixels are not square any more |
| `Letterbox` (default) | As big as it can be with the same shape: bars on two sides |
| `IntegerLetterbox` | A whole number of window pixels per logical pixel (pixel art stays sharp); smaller than the logical size, it is a `Letterbox` |
| `FitHeight` | The height fills it: a wider window shows more at the sides |

`camera.view(windowSize)` says where the world goes (`area`, without the bars) and how big a unit is (`scale`);
`worldToScreen` and `screenToWorld` use it, so they are inverses in every mode, and `camera.visibleSize(windowSize)`
says how much of the world is shown. Sprites and tilemaps are cut at the edge of the area, so nothing is drawn in the bars.

`camera.viewport` is a `Rect` of the window (none: all of it) where the camera draws: a split screen or a minimap.
The UI is laid out in the viewport of the camera.

If the size of the server's "screen of a player" matters to the rules, give it the same numbers as the logical size, in
one place: it then does not depend on the window of each player.

If an entity also has a `PreviousTransform2D` (the client keeps it up to date), it is drawn **between** its last
two ticks by `Time::alpha`: smooth on any refresh rate.

## The window

`client.window()` is the window of the backend: `setSize(w, h)`, `setFullscreen(on)` (desktop fullscreen, no change of the
video mode; leaving it gives the size back), `fullscreen()`, `setVsync(on)`, `setTitle(...)`. A change comes back as a
`ResizeEvent` at the next frame, and the camera follows it (see above): a game with a "Display" screen needs nothing else.
The dummy backend does the same, so a test can follow it.

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

**Music player.** `pauseMusic()` / `resumeMusic()` keep the place (a paused music is still `musicPlaying()`), `musicPosition()` and `musicLength()`
give seconds for a progress bar (`musicLength()` is 0 when the format does not tell), `seekMusic(seconds)` moves inside it (slow for mp3). To start the
next track, give `{.loop = false, .onFinished = ...}` to `playMusic`: it is called from `audio.update()`, on the game's thread, when the music ends by
itself, never for a looping one or after `stopMusic()`, and it may call `playMusic` again. A music that failed to start is not `musicPlaying()` right
after `playMusic`, and never calls `onFinished`. With `DummyAudio`, `advanceMusic(seconds)` moves the clock and `setMusicLength(seconds)` sets the length.

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

### A chat box

`UiChatLog` is the last lines of a chat in a corner, each one fading after a while, and a field to say one:

```cpp
UiChatLog chat(world(), {.anchor = Anchor::BottomLeft, .lines = 6, .showSeconds = 8.0});     // after installUi()

if (actions.wasPressed(Action::Chat) && !chat.isOpen()) chat.open(world());         // the character of the key that opened it is dropped
if (auto line = chat.submitted(world())) endpoint.send(room, kuge::net::ChatSend{*line});   // Enter: the field closes
if (chat.isOpen() && cancelled) chat.close(world());
room.on<kuge::net::ChatLine>([&](auto, const auto& line) { chat.push(nameOf(line.from) + ": " + line.text, colourOf(line.from)); });
chat.update(world(), dt);                                                           // each tick: the lines fade
```

While the field is open every line stays shown. The game decides what an open field stops (the movement): ask `chat.isOpen()`.
`InputMap::discardTyped()` is what drops the typed character that waits; use it for any field that a key opens.

### The developer console

`ConsoleScene` is a scene pushed over the game (it is `Transparent`, see [03](../03-scenes-systems-and-the-ecs/README.md): the game
is still drawn but stands still while the console has the keyboard). Its commands and variables are the game's: see "The console" in
[12](../12-testing-and-debugging/README.md).

## Tilemaps and animations

- A **`TileMap`** (in the core, so a server can load one to know where the walls are) is a grid of layers of
  tile numbers with a list of solid tiles, with a small text format. The client draws it (culled, without seams)
  with a `Spritesheet` (a picture cut in equal cells).
- An **`Animator`** plays clips of a spritesheet (`.anim` text files: frames, fps, loop, cues that fire events at
  a given frame). `AnimateSprites` runs in the Fixed schedule so animations are as reproducible as the rest.
  `Animator::loop(sheet, fps)` plays every cell of a sheet in turn, without a file.

## Cinematics

An intro, a boss entrance, a tutorial that moves the camera: a **timeline** is a JSON file, one per cinematic (its id is the file name), and
`CinematicScene` plays it over a stage that the game builds.

```json
{ "duration": 6, "tracks": [
  { "kind": "transform", "keys": [ { "t": 0.5, "duration": 2, "ease": "outQuad", "target": "boss", "to": { "position": [400, 120] } } ] },
  { "kind": "camera",    "keys": [ { "t": 2.5, "duration": 0.6, "shake": { "amplitude": 6 } } ] },
  { "kind": "text",      "keys": [ { "t": 1, "duration": 3, "speaker": "Boss", "text": "You came." } ] },
  { "kind": "event",     "keys": [ { "t": 5.5, "name": "open_gate" } ] } ] }
```

```cpp
kuge::CinematicScene::Config config;

config.timeline = std::make_shared<const kuge::Timeline>(kuge::Timeline::load("data/timelines/boss_entrance.json"));   // @throw TimelineError, naming the file and the key
config.font = font;
config.setup = [](kuge::CinematicScene& scene) { /* create the entities, scene.name("boss", boss) */ };
config.onEvent = [](const std::string& name, const kuge::Json& key) { /* "open_gate": the game does it */ };
config.onDone = [](kuge::SceneContext& ctx, bool skipped) { ctx.scenes().change<Level>(); };
ctx().scenes().change<kuge::CinematicScene>(config);
```

| Track | A key needs | What it does |
|---|---|---|
| `transform` | `target`, `to` {field: value}, [`component`, default `Transform2D`] | the fields go from what they are to their values (any described component, see [03](../03-scenes-systems-and-the-ecs/README.md)); a text, a bool or an enum jumps at the end |
| `animation` | `target`, `clip` | `Animator::play(clip, restart)` |
| `camera` | `position` [x, y] and/or `zoom`; or `shake` {amplitude} with a `duration` | the camera moves; a shake is random, fades out and leaves the camera exactly where it was |
| `fade` | `color` [r, g, b, a] in 0..1 | the colour over the screen goes to it (it covers the world, not the subtitle) |
| `audio` | `sound` [`volume`, `bus`: "dialogue"] or `music` [`loop`, `fadeIn`] or `stopMusic` (seconds) | plays through `Audio`; the loaders are the client's asset managers |
| `text` | `text`, [`speaker`] | a subtitle for the `duration` of the key (none: until the next text key; an empty text clears) |
| `event` | `name`, [`data`] | `onEvent`: what a cinematic does to the game (open a door, spawn a boss) is the game's |

Every key has `t` (seconds), may have `duration` (a tween), `ease` (`linear`, `inQuad`, `outQuad`, `inOutQuad`) and must end before the
`duration` of the file. The loader refuses a file with an unknown kind, a negative time, a missing field...; the message begins with the
file name and says `tracks[1].keys[0]`.

- **Time** comes from the fixed tick, never from a clock: the same timeline played twice gives the same positions, and the size of the frames
  does not matter (a tween always ends exactly at its value). A shake draws from a generator of its own with a fixed seed.
- **Skip** (Escape, or the `skipAction` of the game): every key that was left starts and every tween ends, in order of time, so the world is
  as if it had played to the end and the events were fired. The sounds of the skipped keys are not played; the music that should be playing is.
- `TimelinePlayer` (core) alone gives `play`, `pause`, `seek`, `restart`, `skip`, `setSpeed`; `Cinematic` (client) applies a timeline to a
  World, and can run inside any scene for a short effect: call `update(dt)` from a system.
- A key that cannot work (a `target` that is not named in the scene, a field that is not described) is logged with the name of the file
  and ignored: the cinematic goes on.
- **Multiplayer**: a cinematic that changes nothing of the game is played by the client alone. One that stops the game is started by the
  room ([09](../09-the-server/README.md#a-cinematic-that-stops-the-game)): the client pushes the scene when it hears `CinematicStart` and sends `CinematicDone` from `onDone`.

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
