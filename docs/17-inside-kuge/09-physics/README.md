# Step 9: Physics

`kuge-physics` moves bodies, stops them at walls, and says who entered which trigger. It is small (one `.cpp` of
500 lines) and deterministic: the same World gives the same result, bit for bit. This step reads one
`Physics2D::step` from top to bottom.

## One step

```cpp
// modules/physics/src/Physics2D.cpp, Physics2D::step
m_events.clear();
collect(world);        // every Collider + Transform2D, sorted by entity, with its box in world coordinates
moveBodies(world, dt); // the bodies move, one after the other, and stop at walls
findTriggers(world);   // who is inside which trigger now, compared with the last step
```

`installPhysics` adds a system in the `Physics` stage that calls it at each tick: after `Simulation` (where the game
sets velocities), before `Late` (where it reacts).

## Collect, in entity order

```cpp
// Physics2D::collect
auto view = world.view<Collider, Transform2D>();
for (kw::Entity entity : view) {
    m_entities.push_back(entity);
}
std::sort(m_entities.begin(), m_entities.end());   // the ECS order moves (step 3): this one does not
```

Each item gets its axis-aligned box (a circle gets its bounding box too) and a `mover` flag: it has a `Body` that is
not `Static`. Everything that happens after walks these items in **entity order**. That is what makes the physics
deterministic whatever the ECS does with its storage.

## Walls, then bodies

```cpp
// Physics2D::moveBodies
m_solids.clear();
for (std::uint32_t i = 0; i < m_items.size(); ++i) {
    if (!m_items[i].mover && !m_items[i].collider.trigger) {
        m_solids.insert(i, m_items[i].box);                 // a wall: what does not move and stops
    }
}
m_solids.finish();

for (each mover, in entity order) {
    if (body.type == Body::Type::Dynamic) {
        body.velocity += config.gravity * (body.gravityScale * dt);
        body.velocity.y = std::min(body.velocity.y, config.maxFallSpeed);
    }
    body.contacts = {};
    moveAlong(mover, 0, body.velocity.x * dt);             // x first...
    moveAlong(mover, 1, body.velocity.y * dt);             // ...then y
    findContacts(mover);
    item.box = mover.boxAt(mover.transform->position);     // later steps see it where it ended
}
```

`m_solids` is a `SpatialGrid`, a uniform grid of cells (`cellSize`, 64 by default). A query returns the items whose
cells the box touches, **sorted and without duplicates**, so the order of the walls a body meets never depends on
how they were inserted.

Moving along x, then along y, is what makes a body **slide** along a wall. If it hits a floor while moving
diagonally, the y movement stops, and the x movement already happened.

## Sub-steps and the skin

```cpp
// Physics2D::moveAlong
const float reach = std::max(axisOf(mover.half, axis), 1.0f);
const int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(delta) / reach)));

for (int step = 0; step < steps; ++step) {
    position += delta / static_cast<float>(steps);          // never more than half its size at a time
    Aabb box = thinnerAcross(mover.boxAt(position), axis);  // SKIN (0.01 px) off both sides of the other axis
    ... every wall in the box: push the body out along this axis, to the side it came from
    ... the same with the solid tiles, row by row
    if (blocked) {
        velocity[axis] = 0.0f;
        contacts[side] = true;
        return;                                             // the rest of the movement is into the wall
    }
}
```

- **Sub-steps**: a body moves at most half its own size at a time. A body moving 50 px in a tick with a half-width
  of 5 takes 10 small steps, and cannot jump over a 4 px wall.
- **The skin**: a body resting on a floor overlaps it by a float's rounding error. Without the skin, moving along x
  would "hit" the floor it stands on. The box is made 0.01 px thinner across the axis of the movement, so only real
  walls count.

`findContacts` then probes 0.05 px around the body on each side. `contacts` is therefore true for a body pressed
against a wall **even when it did not move** this tick. A platformer's "can jump" is `contacts.down`.

## Triggers: a difference between two steps

```cpp
// Physics2D::findTriggers
m_previousPairs.swap(m_pairs);
m_pairs.clear();
for (each trigger i) {
    for (each item j whose box meets it, compatible layers) {
        if (touches(trigger, other)) {
            m_pairs.push_back({trigger.entity, other.entity});
        }
    }
}
// Both lists are sorted: what is only in one of them entered or left
... in m_pairs only:          {trigger, other, entered = true}
... in m_previousPairs only:  {trigger, other, entered = false}, unless one of them was destroyed
```

A trigger event is the **difference between the overlaps of two steps**, computed at the end positions. The
consequence shows in the lab: sub-steps protect walls, not triggers. A body that crosses a trigger entirely between
two ticks was never inside it at the end of a step, so no event is reported.

## Lab

`example/lab/lab09_physics.cpp`:

```cpp
// Lab 9: a fast body, a thin wall, and two trigger zones on its way
#include "Logger.hpp"
#include "Physics2D.hpp"
#include "PhysicsComponents.hpp"
#include "Transform2D.hpp"
#include <cstdio>
#include <string>

int main()
{
    Logger::logger().enable(false);
    kw::World world;
    kuge::Physics2D physics(kuge::PhysicsConfig{.gravity = {0.0f, 0.0f}});
    const auto make = [&world](float x, kuge::Collider collider) {
        const kw::Entity e = world.create();

        world.add<kuge::Transform2D>(e, kuge::Transform2D{{x, 0.0f}});
        world.add<kuge::Collider>(e, collider);
        return e;
    };
    const kw::Entity zoneA = make(40.0f, kuge::Collider::box(20.0f, 20.0f).asTrigger());   // x from 30 to 50
    const kw::Entity zoneB = make(70.0f, kuge::Collider::box(20.0f, 20.0f).asTrigger());   // x from 60 to 80
    make(100.0f, kuge::Collider::box(4.0f, 200.0f));                                       // a wall, x from 98 to 102
    const kw::Entity bullet = make(0.0f, kuge::Collider::box(10.0f, 10.0f));
    kuge::Body body;

    body.velocity = {3000.0f, 0.0f};    // 50 pixels per tick: 10 times half its width
    world.add<kuge::Body>(bullet, body);
    for (int tick = 1; tick <= 3; ++tick) {
        physics.step(world, 1.0f / 60.0f);
        const kuge::Body& now = world.get<kuge::Body>(bullet);
        std::string events;

        for (const auto& event : physics.events()) {
            events += std::string(event.trigger == zoneA ? " zone A" : event.trigger == zoneB ? " zone B" : " ?") + (event.entered ? " entered" : " left");
        }
        std::printf("tick %d: x = %5.1f  velocity.x = %6.1f  touching a wall on its right: %-3s  events:%s\n", tick,
            world.get<kuge::Transform2D>(bullet).position.x, now.velocity.x, now.contacts.right ? "yes" : "no", events.empty() ? " none" : events.c_str());
    }
}
```

What it prints (`./build/example/lab09_physics`):

```text
tick 1: x =  50.0  velocity.x = 3000.0  touching a wall on its right: no   events: zone A entered
tick 2: x =  93.0  velocity.x =    0.0  touching a wall on its right: yes  events: zone A left
tick 3: x =  93.0  velocity.x =    0.0  touching a wall on its right: yes  events: none
```

## Reading the output

- **Tick 1**: 50 pixels, in 10 sub-steps of 5. The bullet ends at x = 50, and its box (45 to 55) overlaps zone A
  (30 to 50): `zone A entered`.
- **Tick 2**: it would reach 100, but a sub-step meets the wall (x from 98): it is pushed out to 98 minus its
  half-width, **93**, its x velocity becomes 0, and `contacts.right` is set. It is out of zone A: `zone A left`.
- **Zone B** (60 to 80) was crossed during tick 2, and **no event** says so: at the end of tick 1 the bullet was
  before it, at the end of tick 2 after it. For fast things and triggers, test the swept path yourself (a
  `raycast`), or keep them slow.
- **Tick 3**: the bullet does not move, and still touches the wall on its right: the probe, not a movement, says so.

Next: [Step 10: Bytes on the wire](../10-bytes-on-the-wire/README.md).
