# 06 Physics

`kuge-physics` is 2D collision and movement. It needs only the core, so **a server has it too**: the room and
the client's prediction run exactly the same physics.

## Adding it

```cpp
void Level::onEnter()
{
    kuge::installPhysics(setup(), {.gravity = {0.0f, 900.0f}});
    world().getResource<kuge::Physics2D>().tiles = kuge::makeSolidGrid(map, "ground");   // optional: solid tiles
}
```

`installPhysics` adds the `Physics2D` resource and one system in the **`Physics` stage** of the Fixed schedule.
So the flow of a tick is: your systems in `Simulation` say where things *want* to go (they set velocities),
the physics moves them, your systems in `Late` react to what happened (damage, pickups).

`PhysicsConfig`: `gravity` (pixels per second squared, y points down), `maxFallSpeed`, `cellSize` (of the
broad phase: about the size of your biggest body).

## Components

| Component | Meaning |
|---|---|
| `Collider` | A box or a circle, with an `offset`, a `layer` and a `mask` (what it is, and what it meets), and a `trigger` flag. Needs a `Transform2D`. |
| `Body` | Makes it move: `Kinematic` (by its `velocity`) or `Dynamic` (also pulled by gravity, scaled by `gravityScale`). No `Body`, or `Static`: a wall. |

```cpp
world.add<kuge::Transform2D>(floor, kuge::Transform2D{{200, 300}});
world.add<kuge::Collider>(floor, kuge::Collider::box(400.0f, 20.0f));                // a wall: no Body

world.add<kuge::Collider>(ship, kuge::Collider::box(16.0f, 10.0f));
world.add<kuge::Body>(ship, kuge::Body{kuge::Body::Type::Kinematic});
world.get<kuge::Body>(ship).velocity = {150.0f, 0.0f};                                // pixels per second

world.add<kuge::Collider>(coin, kuge::Collider::circle(8.0f).asTrigger());
```

## What it does, and does not

- Bodies move along x then along y, in steps smaller than themselves, so a fast body cannot cross a thin
  wall, and they **slide** along walls.
- `body.contacts` says which sides touch a wall (a collider with no body, or a solid tile), **even when the body
  stands still**. That is what a "can I jump?" test reads.
- Bodies are **boxes**; circles are walls, triggers and things to query.
- Bodies do **not** stop each other. To know who touches whom, use a trigger, or your own test. (R-Type's bullets
  and enemies use a simple box overlap test in the rules, not the physics.)
- **Triggers** stop nothing: `physics.events()` lists who **entered** or **left** during the last tick.
- **Layers**: two things meet only if each is on a layer the other reacts to
  (`(a.layer & b.mask) && (b.layer & a.mask)`). A common mistake is giving a hero a `mask` that does not include
  the coins' layer, so the coin trigger never fires.
- No one-way platforms, no rotation, no body-body resolution.

## Questions about the world

```cpp
auto& physics = world.getResource<kuge::Physics2D>();

auto hit = physics.raycast(origin, direction, maxDistance);        // std::optional<RaycastHit>
auto near = physics.overlapRect({x, y, w, h});                     // entities, by increasing entity
auto ring = physics.overlapCircle(center, radius);
bool solid = physics.tiles.isSolidAt(x, y);
```

They describe the world as the last tick left it (nothing is known before the first tick).

## Determinism

**The same world gives the same result, bit for bit**, whatever the ECS does with its storage: entities are
always handled in increasing order. A test replays a run twice and compares every position.

This is not a nicety. It is what lets:

- a **server** and a **client** simulate the same player and agree (see prediction in
  [10](../10-replication-and-prediction/README.md));
- a **test** assert exact positions;
- a **replay** exist.

## Using it without a scene

`Physics2D::step(World&, float dt)` is public. A world that is not a scene (the private world a client
uses to predict its own player) can hold a `Physics2D` resource and call it directly:

```cpp
world.addResource<kuge::Physics2D>(kuge::PhysicsConfig{});
...
world.getResource<kuge::Physics2D>().step(world, 1.0f / 60.0f);
```

R-Type does exactly this, in `stepArena` (`example/rtype/common/RType.hpp`).

Next: [07 Data, saves and config](../07-data-saves-and-config/README.md).
