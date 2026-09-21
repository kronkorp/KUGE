extern "C" {
    #include "kronklab/kronklab.h"
}
#include "physics_fixture.hpp"
#include <algorithm>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using kuge::Collider;
    using kuge::Vec2;
}

Test(physics, a_body_falls)
{
    kw::Entity body = 0;
    Sim sim([&body](Level& level) { body = mover(level.world(), {0.0f, 0.0f}, Collider::box(10.0f, 10.0f), dynamic()); });

    sim.ticks(30);
    // Each tick: the speed grows by g * dt, then it moves by speed * dt
    const float dt = 1.0f / 60.0f;
    const float expected = 1200.0f * dt * dt * (30.0f * 31.0f / 2.0f);

    Assert(near(sim.where(body).y, expected, 0.1f), "fell %f, expected %f", sim.where(body).y, expected);
    Assert(near(sim.body(body).velocity.y, 1200.0f * dt * 30.0f, 0.1f), "at the speed g * t");
    AssertEq(sim.where(body).x, 0.0f, "straight down");
}

Test(physics, falling_has_a_limit)
{
    kw::Entity body = 0;
    Sim sim([&body](Level& level) { body = mover(level.world(), {0.0f, 0.0f}, Collider::box(10.0f, 10.0f), dynamic()); },
        kuge::PhysicsConfig{.gravity = {0.0f, 1200.0f}, .maxFallSpeed = 500.0f});

    sim.ticks(120);
    AssertEq(sim.body(body).velocity.y, 500.0f, "no faster than the limit");
}

Test(physics, gravity_scale)
{
    kw::Entity normal = 0;
    kw::Entity floaty = 0;
    kw::Entity none = 0;
    Sim sim([&](Level& level) {
        kuge::Body light = dynamic();
        kuge::Body weightless = dynamic();

        light.gravityScale = 0.5f;
        weightless.gravityScale = 0.0f;
        normal = mover(level.world(), {0.0f, 0.0f}, Collider::box(4.0f, 4.0f), dynamic());
        floaty = mover(level.world(), {50.0f, 0.0f}, Collider::box(4.0f, 4.0f), light);
        none = mover(level.world(), {100.0f, 0.0f}, Collider::box(4.0f, 4.0f), weightless);
    });

    sim.ticks(30);
    Assert(near(sim.where(floaty).y, sim.where(normal).y * 0.5f, 0.2f), "half the gravity, half the fall");
    AssertEq(sim.where(none).y, 0.0f, "no gravity: it stays");
    Sim kinematicSim([&](Level& level) { none = mover(level.world(), {0.0f, 0.0f}, Collider::box(4.0f, 4.0f), kinematic({})); });
    kinematicSim.ticks(30);
    AssertEq(kinematicSim.where(none).y, 0.0f, "a kinematic body is not pulled");
}

Test(physics, lands_and_stays)
{
    kw::Entity body = 0;
    Sim sim([&body](Level& level) {
        wall(level.world(), {0.0f, 50.0f}, Collider::box(200.0f, 20.0f));              // its top is at y = 40
        body = mover(level.world(), {0.0f, 0.0f}, Collider::box(10.0f, 10.0f), dynamic());
    });

    sim.ticks(60);
    Assert(near(sim.where(body).y, 35.0f, 0.01f), "resting on the floor: its bottom (y + 5) is at 40, got %f", sim.where(body).y);
    Assert(sim.body(body).contacts.down, "it knows it stands on something");
    AssertEq(sim.body(body).velocity.y, 0.0f, "and does not keep its speed");
    const float rest = sim.where(body).y;
    for (int i = 0; i < 100; ++i) {
        sim.ticks(1);
        Assert(near(sim.where(body).y, rest, 0.001f) && sim.body(body).contacts.down, "no jitter at tick %d", i);
    }
}

Test(physics, walls_stop_and_slide)
{
    kw::Entity body = 0;
    Sim sim([&body](Level& level) {
        wall(level.world(), {100.0f, 0.0f}, Collider::box(20.0f, 20000.0f));            // its left face is at x = 90, and it is very tall: the body falls along it
        body = mover(level.world(), {0.0f, 0.0f}, Collider::box(10.0f, 10.0f), dynamic({300.0f, 0.0f}));
    });

    sim.ticks(60);
    Assert(near(sim.where(body).x, 85.0f, 0.01f), "stopped by the wall: right side at 90, got x = %f", sim.where(body).x);
    AssertEq(sim.body(body).velocity.x, 0.0f, "no sideways speed left");
    Assert(sim.body(body).contacts.right, "and it still touches the wall on its right, though it stands still");
    const float y = sim.where(body).y;
    sim.body(body).velocity.x = 300.0f;   // pushing against the wall while falling
    sim.ticks(10);
    Assert(sim.where(body).y > y + 10.0f, "it slides down along the wall");
    Assert(near(sim.where(body).x, 85.0f, 0.01f), "without sinking into it");
}

Test(physics, every_side)
{
    kw::Entity left = 0, up = 0, down = 0, right = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();
        wall(w, {-50.0f, 0.0f}, Collider::box(10.0f, 100.0f));
        wall(w, {0.0f, -100.0f}, Collider::box(100.0f, 10.0f));
        left = mover(w, {-40.0f, 0.0f}, Collider::box(10.0f, 10.0f), kinematic({-100.0f, 0.0f}));
        up = mover(w, {0.0f, -80.0f}, Collider::box(10.0f, 10.0f), kinematic({0.0f, -100.0f}));
        wall(w, {50.0f, 200.0f}, Collider::box(10.0f, 10.0f));
        right = mover(w, {35.0f, 200.0f}, Collider::box(10.0f, 10.0f), kinematic({100.0f, 0.0f}));
        wall(w, {300.0f, 300.0f}, Collider::box(100.0f, 10.0f));
        down = mover(w, {300.0f, 280.0f}, Collider::box(10.0f, 10.0f), kinematic({0.0f, 100.0f}));
    });

    sim.ticks(30);
    Assert(sim.body(left).contacts.left && !sim.body(left).contacts.right, "left");
    Assert(sim.body(up).contacts.up && !sim.body(up).contacts.down, "up");
    Assert(sim.body(right).contacts.right && !sim.body(right).contacts.left, "right");
    Assert(sim.body(down).contacts.down && !sim.body(down).contacts.up, "down");
    Assert(near(sim.where(left).x, -40.0f, 0.01f), "the left body stops against the wall (x = %f)", sim.where(left).x);
    sim.body(left).velocity = {100.0f, 0.0f};
    sim.ticks(1);
    Assert(!sim.body(left).contacts.any(), "contacts are those of the last tick only");
}

Test(physics, fast_bodies_do_not_tunnel)
{
    kw::Entity bullet = 0;
    Sim sim([&bullet](Level& level) {
        // Thin: its left face is at 249. The bullet is at 0, 100, 200, 300...: one big step
        // would put it at 200 (up to 202), then at 300 (from 298): it never lands on the wall
        wall(level.world(), {250.0f, 0.0f}, Collider::box(2.0f, 100.0f));
        bullet = mover(level.world(), {0.0f, 0.0f}, Collider::box(4.0f, 4.0f), kinematic({6000.0f, 0.0f}));   // 100 px a tick
    });

    sim.ticks(10);
    Assert(near(sim.where(bullet).x, 247.0f, 0.01f), "stopped by the thin wall, not through it: %f", sim.where(bullet).x);
    Assert(sim.body(bullet).contacts.right, "and it knows");
    AssertEq(sim.body(bullet).velocity.x, 0.0f, "with no speed left");
}

Test(physics, layers_decide_who_meets)
{
    kw::Entity ghost = 0, solid = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();
        // The wall is on layer 1, and reacts to layers 1 and 4 only
        wall(w, {100.0f, 0.0f}, Collider::box(20.0f, 400.0f).onLayer(1, 1 | 4));
        solid = mover(w, {0.0f, 0.0f}, Collider::box(10.0f, 10.0f).onLayer(1), kinematic({300.0f, 0.0f}));
        ghost = mover(w, {0.0f, 50.0f}, Collider::box(10.0f, 10.0f).onLayer(2), kinematic({300.0f, 0.0f}));
    });

    sim.ticks(60);
    Assert(near(sim.where(solid).x, 85.0f, 0.01f), "layer 1 is stopped");
    Assert(sim.where(ghost).x > 300.0f, "layer 2 goes through: %f", sim.where(ghost).x);
    Assert(kuge::compatible(Collider::box(1, 1).onLayer(1, 2), Collider::box(1, 1).onLayer(2, 1)), "both agree");
    Assert(!kuge::compatible(Collider::box(1, 1).onLayer(1, 2), Collider::box(1, 1).onLayer(2, 4)), "one does not");
}

Test(physics, bodies_pass_each_other)
{
    kw::Entity a = 0, b = 0;
    Sim sim([&](Level& level) {
        a = mover(level.world(), {0.0f, 0.0f}, Collider::box(10.0f, 10.0f), kinematic({120.0f, 0.0f}));
        b = mover(level.world(), {100.0f, 0.0f}, Collider::box(10.0f, 10.0f), kinematic({-120.0f, 0.0f}));
    });

    sim.ticks(60);
    Assert(sim.where(a).x > 100.0f && sim.where(b).x < 0.0f, "bodies do not stop each other: %f %f", sim.where(a).x, sim.where(b).x);
}

Test(physics, statics_never_move)
{
    kw::Entity floor = 0;
    Sim sim([&floor](Level& level) {
        floor = wall(level.world(), {0.0f, 50.0f}, Collider::box(200.0f, 20.0f));
        mover(level.world(), {0.0f, 0.0f}, Collider::box(10.0f, 10.0f), dynamic({50.0f, 0.0f}));
    });

    sim.ticks(120);
    Assert(sim.where(floor) == Vec2(0.0f, 50.0f), "a wall stays where it is");
    Sim same([](Level& level) {
        kuge::Body still;
        still.type = kuge::Body::Type::Static;
        still.velocity = {100.0f, 100.0f};
        mover(level.world(), {5.0f, 5.0f}, Collider::box(10.0f, 10.0f), still);
    });
    same.ticks(10);
    Assert(same.world().get<kuge::Transform2D>(*same.world().view<kuge::Body>().begin()).position == Vec2(5.0f, 5.0f),
        "and so does a Body of type Static, whatever its velocity");
}

Test(physics, circles_are_walls_too)
{
    kw::Entity center = 0, high = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();
        wall(w, {100.0f, 0.0f}, Collider::circle(20.0f));
        center = mover(w, {0.0f, 0.0f}, Collider::box(10.0f, 10.0f), kinematic({300.0f, 0.0f}));
        high = mover(w, {0.0f, 15.0f}, Collider::box(10.0f, 10.0f), kinematic({300.0f, 0.0f}));   // its top edge is 10 below the middle
    });

    sim.ticks(60);
    Assert(near(sim.where(center).x, 75.0f, 0.02f), "head on: its right side stops at 80 (x = %f)", sim.where(center).x);
    // At 10 from the middle the circle is sqrt(20^2 - 10^2) = 17.32 wide on each side
    Assert(near(sim.where(high).x, 100.0f - 17.3205f - 5.0f, 0.02f), "off center it goes further in: %f", sim.where(high).x);
}

Test(physics, offset_and_scale)
{
    kw::Entity body = 0;
    Sim sim([&body](Level& level) {
        auto& w = level.world();
        Collider box = Collider::box(10.0f, 10.0f);

        box.offset = {10.0f, 0.0f};
        wall(w, {100.0f, 0.0f}, Collider::box(20.0f, 400.0f));                     // its left face is at 90
        body = mover(w, {0.0f, 0.0f}, box, kinematic({300.0f, 0.0f}));
        w.get<kuge::Transform2D>(body).scale = {2.0f, 1.0f};                       // 20 wide, and its offset is 20
    });

    sim.ticks(30);
    // The box is centered 20 to the right of the position, and 10 to each side of that: its right side is at x + 30
    Assert(near(sim.where(body).x, 60.0f, 0.01f), "stopped where x + 30 = 90: got %f", sim.where(body).x);
}

Test(physics, solid_tiles)
{
    kw::Entity body = 0, walker = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();
        auto& tiles = w.getResource<kuge::Physics2D>().tiles;

        tiles.reset(20, 10, 16.0f);
        for (int x = 0; x < 20; ++x) {
            tiles.set(x, 9, true);                      // the floor: its top is at y = 144
        }
        for (int y = 0; y < 10; ++y) {
            tiles.set(12, y, true);                     // a wall: its left face is at x = 192
        }
        body = mover(w, {100.0f, 0.0f}, Collider::box(10.0f, 10.0f), dynamic());
        walker = mover(w, {20.0f, 130.0f}, Collider::box(10.0f, 10.0f), dynamic({200.0f, 0.0f}));
    });

    sim.ticks(120);
    Assert(near(sim.where(body).y, 139.0f, 0.01f), "lands on the floor tiles (%f)", sim.where(body).y);
    Assert(sim.body(body).contacts.down, "standing");
    Assert(near(sim.where(walker).x, 187.0f, 0.01f), "the walker stops at the wall tiles (%f)", sim.where(walker).x);
    Assert(sim.body(walker).contacts.right && sim.body(walker).contacts.down, "against the wall, on the floor");
}

Test(physics, wide_bodies_on_tiles)
{
    kw::Entity body = 0;
    Sim sim([&body](Level& level) {
        auto& tiles = level.world().getResource<kuge::Physics2D>().tiles;

        tiles.reset(10, 5, 16.0f, {-32.0f, 0.0f});      // an origin that is not (0, 0)
        for (int x = 0; x < 10; ++x) {
            tiles.set(x, 4, true);                      // top at y = 64
        }
        body = mover(level.world(), {0.0f, 0.0f}, Collider::box(40.0f, 8.0f), dynamic());   // wider than 2 tiles
    });

    sim.ticks(90);
    Assert(near(sim.where(body).y, 60.0f, 0.01f), "rests on the floor: %f", sim.where(body).y);
}

Test(physics, triggers_report)
{
    kw::Entity zone = 0, body = 0;
    std::vector<kuge::TriggerEvent> seen;
    Sim sim([&](Level& level) {
        auto& w = level.world();

        zone = wall(w, {100.0f, 0.0f}, Collider::box(40.0f, 40.0f).asTrigger());      // from x = 80 to 120
        body = mover(w, {0.0f, 0.0f}, Collider::box(10.0f, 10.0f), kinematic({120.0f, 0.0f}));
    });

    for (int i = 0; i < 90; ++i) {
        sim.ticks(1);
        for (const auto& event : sim.physics().events()) {
            seen.push_back(event);
        }
    }
    AssertEq(seen.size(), 2, "one enter, one exit, got %zu", seen.size());
    Assert(seen[0].entered && seen[0].trigger == zone && seen[0].other == body, "it entered the zone");
    Assert(!seen[1].entered && seen[1].trigger == zone && seen[1].other == body, "and left it");
    Assert(near(sim.where(body).x, 180.0f, 0.5f), "and nothing stopped it on the way: 90 ticks at 2 px = 180 (got %f)", sim.where(body).x);
    Assert(!sim.body(body).contacts.any(), "a trigger is not a wall");
    sim.ticks(1);
    AssertEq(sim.physics().events().size(), 0, "events are those of the last tick only");
}

Test(physics, triggers_between_triggers)
{
    Sim sim([](Level& level) {
        wall(level.world(), {0.0f, 0.0f}, Collider::circle(10.0f).asTrigger());
        wall(level.world(), {5.0f, 0.0f}, Collider::box(10.0f, 10.0f).asTrigger());
    });

    sim.ticks(1);
    AssertEq(sim.physics().events().size(), 1, "two triggers meeting: reported once, got %zu", sim.physics().events().size());
    Assert(sim.physics().events()[0].entered, "as an entry");
    sim.ticks(5);
    AssertEq(sim.physics().events().size(), 0, "and not again while they stay");
}

Test(physics, triggers_follow_layers)
{
    Sim sim([](Level& level) {
        auto& w = level.world();

        wall(w, {0.0f, 0.0f}, Collider::box(20.0f, 20.0f).onLayer(1, 2).asTrigger());   // reacts to layer 2 only
        wall(w, {0.0f, 0.0f}, Collider::box(10.0f, 10.0f).onLayer(4));                  // layer 4
        wall(w, {0.0f, 0.0f}, Collider::box(10.0f, 10.0f).onLayer(2, 1));               // layer 2
    });

    sim.ticks(1);
    AssertEq(sim.physics().events().size(), 1, "only what the trigger reacts to, got %zu", sim.physics().events().size());
}

Test(physics, a_gone_body_does_not_leave)
{
    kw::Entity body = 0;
    Sim sim([&body](Level& level) {
        wall(level.world(), {0.0f, 0.0f}, Collider::box(40.0f, 40.0f).asTrigger());
        body = mover(level.world(), {0.0f, 0.0f}, Collider::box(4.0f, 4.0f), kinematic({}));
    });

    sim.ticks(2);
    sim.world().remove(body);
    sim.ticks(1);
    AssertEq(sim.physics().events().size(), 0, "destroyed: it is gone, it did not walk out");
    sim.ticks(3);
}

Test(physics, stages_are_in_order)
{
    struct Watch : kw::ISystem
    {
        Watch(kw::Entity e, float* seen) : m_entity(e), m_seen(seen) {}
        bool handle(kw::World& world) override { *m_seen = world.get<kuge::Transform2D>(m_entity).position.x; return true; }
        kw::Entity m_entity; float* m_seen;
    };
    kw::Entity body = 0;
    float beforePhysics = -1.0f;
    float afterPhysics = -1.0f;
    Sim sim([&](Level& level) {
        body = mover(level.world(), {0.0f, 0.0f}, Collider::box(4.0f, 4.0f), kinematic({60.0f, 0.0f}));
        level.addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Watch>(body, &beforePhysics));
        level.addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<Watch>(body, &afterPhysics));
    });

    sim.ticks(1);
    AssertEq(beforePhysics, 0.0f, "Simulation runs before the bodies move");
    Assert(near(afterPhysics, 1.0f, 1e-4f), "Late sees them moved, in the same tick: %f", afterPhysics);
}

Test(physics, contacts_need_no_speed)
{
    kw::Entity onFloor = 0, atWall = 0, inAir = 0, wedged = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();

        wall(w, {0.0f, 50.0f}, Collider::box(400.0f, 20.0f));                    // top at 40
        wall(w, {150.0f, 0.0f}, Collider::box(20.0f, 200.0f));                   // left face at 140
        wall(w, {-100.0f, 0.0f}, Collider::box(10.0f, 200.0f));                  // right face at -95
        onFloor = mover(w, {0.0f, 35.0f}, Collider::box(10.0f, 10.0f), kinematic({}));   // its bottom is at 40, exactly
        atWall = mover(w, {135.0f, 0.0f}, Collider::box(10.0f, 10.0f), kinematic({}));    // its right side is at 140
        inAir = mover(w, {50.0f, 0.0f}, Collider::box(10.0f, 10.0f), kinematic({}));
        wedged = mover(w, {-90.0f, 35.0f}, Collider::box(10.0f, 10.0f), kinematic({}));   // on the floor, and against a wall
    });

    sim.ticks(1);
    Assert(sim.body(onFloor).contacts.down && !sim.body(onFloor).contacts.up, "standing, at rest: it touches the floor");
    Assert(sim.body(atWall).contacts.right && !sim.body(atWall).contacts.left, "at rest against a wall on its right");
    Assert(!sim.body(inAir).contacts.any(), "in the air: nothing");
    Assert(sim.body(wedged).contacts.down && sim.body(wedged).contacts.left, "two sides at once");
    sim.body(inAir).velocity = {0.0f, 0.0f};
    sim.world().get<kuge::Transform2D>(onFloor).position.y -= 1.0f;   // one pixel above the floor: not touching
    sim.ticks(1);
    Assert(!sim.body(onFloor).contacts.down, "a pixel above the floor is not touching it");
}

// A body that walks on a floor sits on it with the rounding error of a float:
// it must not be taken for a wall in its way
Test(physics, walking_on_any_floor)
{
    std::uint32_t state = 99;
    auto random = [&state](float low, float high) {
        state = state * 1664525u + 1013904223u;
        return low + (high - low) * static_cast<float>(state >> 8) / static_cast<float>(1 << 24);
    };

    for (int trial = 0; trial < 60; ++trial) {
        const float floorY = random(30.0f, 300.0f);
        const float floorHeight = random(5.0f, 40.0f);
        const float width = random(3.0f, 30.0f);
        const float height = random(3.0f, 30.0f);
        const float speed = random(60.0f, 300.0f);
        kw::Entity body = 0;
        Sim sim([&](Level& level) {
            wall(level.world(), {0.0f, floorY}, Collider::box(4000.0f, floorHeight));
            body = mover(level.world(), {-500.0f, floorY - floorHeight / 2 - height / 2 - 20.0f},
                Collider::box(width, height), dynamic());
        });

        sim.ticks(60);                                       // it falls, and lands
        Assert(sim.body(body).contacts.down, "trial %d: it stands on the floor", trial);
        const float x0 = sim.where(body).x;

        for (int tick = 1; tick <= 40; ++tick) {
            sim.body(body).velocity.x = speed;
            sim.ticks(1);
            Assert(!sim.body(body).contacts.right, "trial %d, tick %d: the floor is not a wall", trial, tick);
            Assert(near(sim.where(body).x, x0 + speed / 60.0f * static_cast<float>(tick), 0.05f),
                "trial %d, tick %d: it walks at its speed (x = %f)", trial, tick, sim.where(body).x);
        }
    }
}
