# Step 8: The client

This step follows two paths through `kuge-client`: a key, from the operating system to the `ActionState` a system
reads, and an entity, from its `Transform2D` to a rectangle on the screen. Then it opens the asset cache.

## Backends

The client never calls SDL directly. It talks to three interfaces, `IWindow`, `IInputSource` and `IRenderer2D`,
plus two optional ones, `IAudio` and `IFontLoader`, gathered in a `Backend`. `makeSdlBackend()` builds them on SDL2.
`makeDummyBackend()` builds versions that draw nothing and **record** every call, which is what the tests and this
step's lab use.

## A key, to an action

**1. The event.** In `ClientModule::beginFrame` (step 6), the backend's events go to `InputMap::handle`:

```cpp
// modules/client/src/input/InputMap.cpp
void InputMap::handle(const Event& event)
{
    ... (capture mode, for rebinding: omitted)
    apply(event);          // the raw state: m_keys[Space] = down, a mouse button, a pad axis...
    refresh(true);
}

void InputMap::refresh(bool latchEdges)
{
    const std::uint64_t now = computeDown();     // for each action: is any of its bindings down?

    if (latchEdges) {
        m_pressed |= now & ~m_down;              // went down since the last tick: kept
        m_released |= ~now & m_down;             // went up since the last tick: kept
    }
    m_down = now;
}
```

An action is a bit in a `uint64_t` (hence 64 actions at most). `m_pressed` and `m_released` are **latched**: they
are ORed in, event after event, and nothing clears them yet.

**2. The tick.** `installClientSystems()` puts `SampleInput` in the Input stage of the Fixed schedule:

```cpp
// modules/client/src/render/Systems.cpp, SampleInput::handle
world.getResource<ActionState>() =
    world.getResource<Ref<InputMap>>()->sampleTick(world.getResource<Time>().tick);

// modules/client/src/input/InputMap.cpp, InputMap::sampleTick
ActionState state;

state.tick = tick;
state.down = m_down;
state.pressed = m_pressed;
state.released = m_released;
m_pressed = 0;                                  // edges belong to the tick that saw them
m_released = 0;
return state;
```

So:

- A key pressed **and released** between two ticks still gives `wasPressed` to the next tick: the edge was latched.
- If one loop runs two ticks, only the first sees `wasPressed`. An edge is given to exactly one tick.
- The game reads `ActionState`, a value of its own World, and never a key. A rebinding, a gamepad, or a scripted
  test pilot changes nothing in the game.

## An entity, to a rectangle

**1. Remember where it was.** `SnapshotTransforms` (Fixed, Input stage, so before the game moves anything) copies
each `Transform2D` into its `PreviousTransform2D`, for the entities that have one.

**2. Draw.** `SpriteRender` (Frame, Render stage):

```cpp
// modules/client/src/render/Systems.cpp, SpriteRender::handle
auto view = world.view<Transform2D, Sprite>();
for (kw::Entity entity : view) {
    ...
    if (world.has<PreviousTransform2D>(entity)) {       // between the last two ticks
        transform = lerp(world.get<PreviousTransform2D>(entity).value, transform, alpha);
    }
    size = Vec2{size.x * transform.scale.x, size.y * transform.scale.y} * camera.zoom;
    const Vec2 at = camera.worldToScreen(transform.position, screen);
    const Rect destination{at.x - sprite.pivot.x * size.x, at.y - sprite.pivot.y * size.y, size.x, size.y};
    ... skip it if it is off the screen (a turned sprite may reach as far as its diagonal)
    m_items.push_back(item);
}
... tilemaps become items too
// A total order: the entity breaks the ties, so the result does not depend on how the World stores them
std::sort(m_items.begin(), m_items.end(), [](const Item& a, const Item& b) {
    if (a.layer != b.layer) { return a.layer < b.layer; }
    if (a.z != b.z) { return a.z < b.z; }
    if (a.texture != b.texture) { return a.texture < b.texture; }
    return a.entity < b.entity;
});
for (const Item& item : m_items) {
    ... fillRect for a plain, straight rectangle; drawTexture otherwise
}
```

- `alpha` is the leftover of the accumulator (step 2). Drawing at `lerp(previous, current, alpha)` means the picture
  is **one tick behind** the simulation, in exchange for moving smoothly on any screen.
- Sorting by texture inside a layer groups the draws that use the same texture. Sorting by entity last makes the
  order total, so two runs draw the same picture: the screenshot tests depend on it.
- Tilemaps draw only the visible tiles. Their edges are rounded once per column and per row, so two neighbouring
  tiles always share an edge, and no seam shows at any zoom.

`ClientModule::endFrame` then calls `renderer.present()`.

## Assets: one cache, weak references

```cpp
// modules/core/src/AssetManager.hpp, AssetManager<T>::load
std::lock_guard lock(m_mutex);
const std::string key = path.lexically_normal().string();
auto found = m_cache.find(key);

if (found != m_cache.end()) {
    if (auto alive = found->second.asset.lock()) {
        return alive;                                    // somebody still holds it: the same object
    }
}
const Stamp stamp = stampOf(path);                       // date and size, before reading
std::shared_ptr<T> asset = m_loader(path);
m_cache[key] = Entry{asset, path, stamp};                // the cache keeps a weak_ptr
return asset;
```

- The cache holds `weak_ptr`s. An asset lives while a component (a `Sprite`) holds it, and is freed when the last
  one lets go. The next `load` reads the file again.
- **In the background** (`loadAsync`): the first half, file to pixels, runs on a worker (`prepare`); the second half,
  pixels to texture, runs in `pump()`, which `beginFrame` calls on the main thread. A texture belongs to the
  renderer's thread; decoding does not.
- **Hot reload** (`reloadChanged`) compares each file's date and size with the stamp taken when it was loaded, and
  reloads the object **in place**, so every sprite that holds it draws the new picture.

## Lab

`example/lab/lab08_client.cpp`:

```cpp
// Lab 8: the client with no screen. A key tapped between two ticks, and a sprite drawn between two ticks.
// The dummy backend records what it is asked to draw: that is what we print.
#include "ClientModule.hpp"
#include "ClientScene.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include "backend/dummy/DummyBackend.hpp"
#include <cstdio>
#include <functional>

namespace
{
    enum class Action : std::uint8_t { Fire };

    class Say final : public kw::ISystem
    {
        public:
            explicit Say(std::function<void(kw::World&)> fn) : m_fn(std::move(fn)) {}
            bool handle(kw::World& world) override { m_fn(world); return true; }

        private:
            std::function<void(kw::World&)> m_fn;
    };

    class Demo final : public kuge::ClientScene
    {
        public:
            void onEnter(void) override
            {
                installClientSystems();
                const kw::Entity mover = world().create();
                kuge::Sprite sprite;

                sprite.size = {10.0f, 10.0f};
                world().add<kuge::Transform2D>(mover, kuge::Transform2D{{0.0f, 0.0f}});
                world().add<kuge::PreviousTransform2D>(mover, kuge::PreviousTransform2D{kuge::Transform2D{{0.0f, 0.0f}}});
                world().add<kuge::Sprite>(mover, sprite);
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Say>([mover](kw::World& w) {
                    const auto tick = static_cast<unsigned long long>(w.getResource<kuge::Time>().tick);

                    w.get<kuge::Transform2D>(mover).position.x += 6.0f;     // 6 pixels per tick
                    if (w.getResource<kuge::ActionState>().wasPressed(Action::Fire)) {
                        std::printf("    tick %llu sees Fire pressed\n", tick);
                    }
                }));
            }
    };
}

int main()
{
    Logger::logger().enable(false);
    kuge::DummyBackend dummy = kuge::makeDummyBackend({320.0f, 240.0f});
    kuge::DummyInput* keyboard = dummy.input;
    kuge::DummyRenderer* screen = dummy.renderer;
    kuge::Engine engine({.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
    auto& client = engine.addModule<kuge::ClientModule>(std::move(dummy.backend));

    client.input().declare(Action::Fire, "fire");
    client.input().bind(Action::Fire, kuge::Key::Space);
    engine.scenes().change<Demo>();
    for (int frame = 1; frame <= 8; ++frame) {
        if (frame == 3) {
            // Pressed and released before the next tick: the tick must still see it
            keyboard->push(kuge::KeyEvent{kuge::Key::Space, true});
            keyboard->push(kuge::KeyEvent{kuge::Key::Space, false});
            std::printf("  (Space tapped before frame 3)\n");
        }
        engine.step(1.0 / 120.0);     // a 120 Hz screen: half a tick per frame
        const auto& drawn = screen->lastFrame().front();
        const float x = drawn.destination.x + drawn.destination.w / 2 - 160.0f;    // back to the world (camera at 0, zoom 1)

        std::printf("frame %d: alpha %.1f, the sprite is drawn at x = %4.1f\n", frame, engine.time().alpha, x);
    }
}
```

What it prints (`./build/example/lab08_client`):

```text
frame 1: alpha 0.5, the sprite is drawn at x =  0.0
frame 2: alpha 0.0, the sprite is drawn at x =  0.0
  (Space tapped before frame 3)
frame 3: alpha 0.5, the sprite is drawn at x =  3.0
    tick 1 sees Fire pressed
frame 4: alpha 0.0, the sprite is drawn at x =  6.0
frame 5: alpha 0.5, the sprite is drawn at x =  9.0
frame 6: alpha 0.0, the sprite is drawn at x = 12.0
frame 7: alpha 0.5, the sprite is drawn at x = 15.0
frame 8: alpha 0.0, the sprite is drawn at x = 18.0
```

## Reading the output

- At 120 Hz, each frame is half a tick, so frames alternate between no tick (`alpha 0.5`) and one tick
  (`alpha 0.0`).
- The sprite moves 6 pixels per tick, but from frame 2 on it is **drawn** 3 pixels further at every frame: 0, 0, 3,
  6, 9... Each frame shows it between its last two positions. The price is visible too: after tick 0 moved it to 6
  (frame 2), it is still drawn at 0. The picture is one tick behind.
- Space was pressed and released before frame 3, a frame that ran no tick. The edge waited, latched, and tick 1
  (during frame 4) saw `Fire pressed`, once.
- The positions come from the dummy renderer's record of what it was asked to draw (`lastFrame()`), converted back
  from screen to world coordinates.

Next: [Step 9: Physics](../09-physics/README.md).
