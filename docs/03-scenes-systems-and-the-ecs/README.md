# 03 Scenes, systems and the ECS

Three ideas are enough to write a game with KUGE: **entities with components** hold the state,
**systems** change it, **scenes** group them.

## The ECS (kronkworld)

An **entity** is a number. A **component** is a plain struct attached to it. A **system** is code
that looks at the entities that have some components.

```cpp
struct Health { int points = 100; };

kw::World world;
kw::Entity ship = world.create();

world.add<kuge::Transform2D>(ship, kuge::Transform2D{{40.0f, 100.0f}});
world.add<Health>(ship, Health{5});

world.get<Health>(ship).points -= 1;
if (world.has<Health>(ship)) { ... }
world.remove<Health>(ship);      // one component
world.remove(ship);              // the entity and all its components
```

To handle every entity that has some components, use a **view**:

```cpp
auto view = world.view<kuge::Transform2D, Health>();

for (kw::Entity entity : view) {
    world.get<Health>(entity).points += 1;
}
```

A **resource** is a value that exists once per World (a clock, a camera, a score, a service):

```cpp
world.addResource<Score>();
world.getResource<Score>().points += 10;
```

Things to know (the [pitfalls](../14-pitfalls/README.md) page has more):

- Components are **plain structs**, added with aggregate initialization: `world.add<C>(e, C{...})`.
- A view visits entities **in the order the ECS stores them, which changes when entities are
  removed**. Anything that must be reproducible (physics, rules, events, drawing order) has to
  sort by entity number first. The physics and the R-Type rules do.
- There are at most **256 component types** and **256 resource types** per process.
- `world.remove(entity)` while you walk a view is a trap: collect the entities first, then remove.

## Systems

A system derives from `kw::ISystem` and implements `handle(World&)`. It is called once per tick (or
frame), and returns `true` to be called again, `false` to be removed.

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
            return true;
        }
};
```

A system that keeps state (a timer, a pointer to the scene) simply has members. Several games in
this repository also use a tiny adapter, a system made of a function:

```cpp
class Work final : public kw::ISystem
{
    public:
        explicit Work(std::function<void(kw::World&)> fn) : m_fn(std::move(fn)) {}
        bool handle(kw::World& world) override { m_fn(world); return true; }
    private:
        std::function<void(kw::World&)> m_fn;
};

addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Work>([this](kw::World& w) { tick(w); }));
```

**A system that throws** ends that pass (the systems after it do not run) and the exception comes
out of the scene's tick, so `run()` and `step()` report it. The engine catches it inside the
scheduler so that nothing is left half done.

## Scenes

A **scene** is a part of a game: a menu, a level, a pause screen, a lobby, a room. It owns its own
World, and says what it does by adding systems. It never says *where or when* it runs.

```cpp
class Level : public kuge::Scene
{
    public:
        void onEnter() override
        {
            addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<MoveRight>());
            const kw::Entity e = world().create();
            world().add<kuge::Transform2D>(e, kuge::Transform2D{});
        }
        void onExit() override { /* the systems are removed right after */ }
        void onPause() override {}            // another scene was pushed over this one
        void onResume() override {}           // and it was popped
        void onMessage(const kuge::Message&) override {}   // see 04
};
```

What a scene has:

| | |
|---|---|
| `world()` | Its World |
| `addSystem(schedule, stage, system, delay, interval)` | Adds a system, remembered so that it is removed when the scene is left. `delay` and `interval` (in ticks) make it run later or less often. |
| `ctx()` | The engine, the scene stack, the clock, and handles to talk to other scenes |
| `setup()` | A `SceneSetup`: what a function that installs things (`installPhysics`, `installNet`) needs |

Scenes live on a **stack**, and only the top one runs:

```cpp
engine.scenes().change<MainMenu>();          // leaves everything, enters a MainMenu (arguments can follow)
engine.scenes().push<PauseMenu>(shared);     // pauses the top scene and enters one over it
engine.scenes().pop();                        // leaves the top one, resumes the one below
```

These calls are **deferred**: a scene is never destroyed while it runs, so it is always safe to call
them from inside a system. They happen between two loops, in the order they were asked.

Constructor arguments given to `change` and `push` are **copied** (or moved) into the scene when it
is made. To share something, pass a `std::shared_ptr` or a pointer. (The platformer passes a
`std::shared_ptr<Shared>` to all its scenes.)

### Install functions

Modules give a scene abilities with a function that takes `setup()`:

```cpp
void Level::onEnter()
{
    installClientSystems();                       // sprites, input sampling, animation (a ClientScene)
    kuge::installPhysics(setup(), {.gravity = {0, 900}});
    kuge::net::installNet(setup());               // a network resource, polled each tick
}
```

Each adds resources to the World and systems to the right stages. The order you install them in
is the order their systems run within a stage.

## Resources that modules give you

`Ref<T>` is a resource that points to a service that lives outside the World (the renderer, the input
map, the audio mixer):

```cpp
world.getResource<kuge::Ref<kuge::InputMap>>()->isDown(Action::Fire);
```

The client injects a `Ref<IRenderer2D>`, `Ref<InputMap>`, `Ref<AssetManager<Texture>>`, `Ref<Audio>`
and more. See [05 The client](../05-the-client/README.md).

## A tiny complete example

A scene with a moving square and a system, headless, for two seconds:

```cpp
struct Square { float speed = 60.0f; };

class Mover : public kw::ISystem
{
    public:
        bool handle(kw::World& world) override
        {
            const float dt = static_cast<float>(world.getResource<kuge::Time>().dt);
            auto view = world.view<Square, kuge::Transform2D>();

            for (kw::Entity e : view) {
                world.get<kuge::Transform2D>(e).position.x += world.get<Square>(e).speed * dt;
            }
            return true;
        }
};

class Demo : public kuge::Scene
{
    public:
        void onEnter() override
        {
            const kw::Entity e = world().create();

            world().add<Square>(e, Square{});
            world().add<kuge::Transform2D>(e, kuge::Transform2D{});
            addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Mover>());
        }
};

int main()
{
    kuge::Engine engine({.tickRate = 60});

    engine.scenes().change<Demo>();
    for (int i = 0; i < 120; ++i) {
        engine.step(1.0 / 60.0);                  // two seconds of game, instantly
    }
}
```

After 120 steps the square has moved 120 pixels, on any machine. That reproducibility is the
reason the rest of the engine (prediction, servers, tests) works.

Next: [04 Threads and messages](../04-threads-and-messages/README.md).
