extern "C" {
    #include "kronklab/kronklab.h"
}
#include "physics3d_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    constexpr float DT = 1.0f / 60.0f;

    kw::Entity person(kw::World& world, Vec3 feet, kuge::Character3D settings = {})
    {
        const kw::Entity entity = world.create();
        kuge::Transform3D transform;

        transform.translation = feet;
        world.add<kuge::Transform3D>(entity, transform);
        world.add<kuge::Character3D>(entity, settings);
        return entity;
    }

    // One tick of the game: the character decides, then the physics steps
    void walk(Sim& sim, kw::Entity who, kuge::CharacterMove move, int ticks)
    {
        for (int i = 0; i < ticks; ++i) {
            sim.physics().moveCharacter(sim.world(), who, move, DT);
            sim.ticks(1);
        }
    }

    kuge::Character3D& state(Sim& sim, kw::Entity who) { return sim.world().get<kuge::Character3D>(who); }

    // A step: a box whose top is `height` above the floor, from z = -1 to the +z side
    kw::Entity wallAt(kw::World& world, Vec3 center, Vec3 size, float turnAroundZ = 0.0f, float turnAroundY = 0.0f)
    {
        const kw::Entity entity = thing(world, center, kuge::Collider3D::box(size.x, size.y, size.z));
        auto& transform = world.get<kuge::Transform3D>(entity);

        if (turnAroundZ != 0.0f) {
            transform.rotation = kuge::axisAngle({0.0f, 0.0f, 1.0f}, turnAroundZ);
        }
        if (turnAroundY != 0.0f) {
            transform.rotation = kuge::axisAngle({0.0f, 1.0f, 0.0f}, turnAroundY);
        }
        return entity;
    }
}

Test(character, lands_and_stands)
{
    kw::Entity who = 0;
    Sim sim([&](Level& level) { floor(level.world()); who = person(level.world(), {0.0f, 2.0f, 0.0f}); });

    sim.ticks(1);
    walk(sim, who, {}, 5);
    Assert(!state(sim, who).onGround && sim.where(who).y < 2.0f, "it falls");
    walk(sim, who, {}, 120);
    Assert(near(sim.where(who).y, 0.0f, 0.03f), "feet on the floor: y = %f", sim.where(who).y);
    Assert(state(sim, who).onGround, "and it knows");
    Assert(near(state(sim, who).velocity.y, 0.0f, 0.2f), "still: %f", state(sim, who).velocity.y);
}

Test(character, walks_at_its_wish)
{
    kw::Entity who = 0;
    Sim sim([&](Level& level) { floor(level.world()); who = person(level.world(), {0.0f, 0.0f, 0.0f}); });

    sim.ticks(1);
    walk(sim, who, {}, 10);
    walk(sim, who, {.wish = {3.0f, 0.0f, 0.0f}}, 60);
    Assert(near(sim.where(who).x, 3.0f, 0.2f), "3 m/s for a second: x = %f", sim.where(who).x);
    Assert(near(sim.where(who).y, 0.0f, 0.03f) && state(sim, who).onGround, "on the floor all along");
    walk(sim, who, {}, 10);
    Assert(near(sim.where(who).x, 3.0f, 0.3f), "no wish, it stops (no inertia in the air of a floor)");
}

Test(character, stops_at_a_wall_and_slides)
{
    kw::Entity head_on = 0, glancing = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();
        floor(w);
        wallAt(w, {3.0f, 1.0f, 0.0f}, {0.5f, 2.0f, 20.0f});                         // a wall across x = 2.75
        head_on = person(w, {0.0f, 0.0f, -8.0f});
        // A wall turned 20 degrees from the way to go: it is a corner to slide along
        wallAt(w, {3.0f, 1.0f, 18.0f}, {0.5f, 2.0f, 10.0f}, 0.0f, 0.35f);
        glancing = person(w, {0.0f, 0.0f, 18.0f});
    });

    sim.ticks(1);
    walk(sim, head_on, {.wish = {4.0f, 0.0f, 0.0f}}, 90);
    Assert(sim.where(head_on).x > 2.0f && sim.where(head_on).x < 2.5f, "stopped by the wall: x = %f", sim.where(head_on).x);
    walk(sim, glancing, {.wish = {4.0f, 0.0f, 0.0f}}, 90);
    Assert(std::fabs(sim.where(glancing).z - 18.0f) > 0.5f, "pushing on a slanting wall, it slides along it: z moved by %f", sim.where(glancing).z - 18.0f);
}

Test(character, steps_up_a_small_step)
{
    kw::Entity low = 0, high = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();
        floor(w);
        wallAt(w, {5.0f, 0.15f, -8.0f}, {5.0f, 0.3f, 4.0f});     // a step of 0.3 m, x from 2.5 to 7.5
        wallAt(w, {5.0f, 0.35f, 8.0f}, {5.0f, 0.7f, 4.0f});      // a step of 0.7 m (a round capsule climbs a little more than stepHeight)
        low = person(w, {0.0f, 0.0f, -8.0f});
        high = person(w, {0.0f, 0.0f, 8.0f});
    });

    sim.ticks(1);
    walk(sim, low, {.wish = {3.0f, 0.0f, 0.0f}}, 90);
    Assert(sim.where(low).x > 3.5f && near(sim.where(low).y, 0.3f, 0.05f), "it climbs 0.3 m: (%f, %f)", sim.where(low).x, sim.where(low).y);
    walk(sim, high, {.wish = {3.0f, 0.0f, 0.0f}}, 90);
    Assert(sim.where(high).x < 2.6f && near(sim.where(high).y, 0.0f, 0.05f), "but not 0.7 m: (%f, %f)", sim.where(high).x, sim.where(high).y);
}

Test(character, slopes_up_to_its_limit)
{
    kw::Entity gentle = 0, steep = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();
        floor(w);
        // Long ramps that rise toward +x (a turn around z, counter-clockwise, lifts the +x end)
        wallAt(w, {3.0f, 0.0f, -8.0f}, {12.0f, 0.4f, 4.0f}, 0.52f);       // 30 degrees
        wallAt(w, {3.0f, 0.0f, 8.0f}, {12.0f, 0.4f, 4.0f}, 1.22f);        // 70 degrees
        gentle = person(w, {0.5f, 0.0f, -8.0f});
        steep = person(w, {0.5f, 0.0f, 8.0f});
    });

    sim.ticks(1);
    walk(sim, gentle, {.wish = {3.0f, 0.0f, 0.0f}}, 120);
    Assert(sim.where(gentle).y > 1.5f && state(sim, gentle).onGround, "a slope of 30 degrees is climbed: y = %f", sim.where(gentle).y);
    walk(sim, steep, {.wish = {3.0f, 0.0f, 0.0f}}, 120);
    Assert(sim.where(steep).y < 0.8f, "one of 70 degrees is a wall: y = %f", sim.where(steep).y);
}

Test(character, jumps_and_lands)
{
    kw::Entity who = 0;
    Sim sim([&](Level& level) { floor(level.world()); who = person(level.world(), {0.0f, 0.0f, 0.0f}); });

    sim.ticks(1);
    walk(sim, who, {}, 10);
    float top = 0.0f;

    walk(sim, who, {.jump = true}, 1);
    for (int i = 0; i < 90; ++i) {
        walk(sim, who, {}, 1);
        top = std::max(top, sim.where(who).y);
    }
    Assert(top > 1.1f && top < 1.4f, "v^2 / 2g = 1.27 m: %f", top);
    Assert(near(sim.where(who).y, 0.0f, 0.03f) && state(sim, who).onGround, "and it lands");
    walk(sim, who, {.jump = true}, 1);
    walk(sim, who, {.jump = true}, 1);
    Assert(sim.where(who).y > 0.0f, "(a held jump jumps again on landing)");
}

Test(character, crouch_needs_room)
{
    kw::Entity who = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();
        floor(w);
        wallAt(w, {5.0f, 1.8f, 0.0f}, {4.0f, 0.8f, 4.0f});    // a ceiling: its underside is at y = 1.4, from x = 3 to 7
        who = person(w, {0.0f, 0.0f, 0.0f});
    });

    sim.ticks(1);
    walk(sim, who, {}, 10);
    walk(sim, who, {.wish = {3.0f, 0.0f, 0.0f}}, 60);
    Assert(sim.where(who).x > 2.5f && sim.where(who).x < 3.2f, "standing, the 1.8 m person is stopped by the ceiling: x = %f", sim.where(who).x);
    walk(sim, who, {}, 5);
    walk(sim, who, {.wish = {3.0f, 0.0f, 0.0f}, .crouch = true}, 90);
    Assert(state(sim, who).crouching && sim.where(who).x > 4.0f, "crouched, it goes under: x = %f", sim.where(who).x);
    walk(sim, who, {.wish = {0.0f, 0.0f, 0.0f}}, 30);
    Assert(state(sim, who).crouching, "released under the ceiling, it cannot stand: still crouched");
    walk(sim, who, {.wish = {3.0f, 0.0f, 0.0f}}, 120);
    Assert(!state(sim, who).crouching && sim.where(who).x > 7.5f, "out from under it, it stands: x = %f", sim.where(who).x);
}

Test(character, fast_does_not_tunnel)
{
    kw::Entity who = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();
        floor(w);
        wallAt(w, {5.0f, 1.0f, 0.0f}, {0.1f, 2.0f, 20.0f});
        who = person(w, {0.0f, 0.0f, 0.0f});
    });

    sim.ticks(1);
    walk(sim, who, {.wish = {100.0f, 0.0f, 0.0f}}, 30);
    Assert(sim.where(who).x < 5.0f, "100 m/s against a wall of 10 cm: x = %f", sim.where(who).x);
}

Test(character, same_inputs_same_places)
{
    const auto run = [] {
        kw::Entity who = 0;
        Sim sim([&](Level& level) {
            auto& w = level.world();
            floor(w);
            wallAt(w, {3.0f, 0.15f, 0.0f}, {1.0f, 0.3f, 4.0f});
            wallAt(w, {6.0f, 0.0f, 3.0f}, {6.0f, 0.4f, 4.0f}, 0.4f);
            who = person(w, {0.0f, 0.0f, 0.0f});
        });

        sim.ticks(1);
        for (int i = 0; i < 200; ++i) {
            walk(sim, who, {.wish = {2.5f, 0.0f, 0.7f * std::sin(static_cast<float>(i) * 0.05f)}, .jump = i % 70 == 0, .crouch = i % 90 > 60}, 1);
        }
        return sim.where(who);
    };
    const Vec3 first = run();
    const Vec3 second = run();

    Assert(first.x == second.x && first.y == second.y && first.z == second.z, "twice the same walk, the same place");
}

Test(character, teleport_and_remove)
{
    kw::Entity who = 0;
    Sim sim([&](Level& level) { floor(level.world()); who = person(level.world(), {0.0f, 0.0f, 0.0f}); });

    sim.ticks(1);
    walk(sim, who, {}, 5);
    sim.world().get<kuge::Transform3D>(who).translation = {10.0f, 0.0f, 10.0f};
    walk(sim, who, {}, 5);
    Assert(near(sim.where(who).x, 10.0f, 0.05f) && near(sim.where(who).z, 10.0f, 0.05f), "put elsewhere by the game, it goes on from there");
    sim.world().remove<kuge::Character3D>(who);
    sim.ticks(2);
    AssertEq(sim.physics().bodyCount(), 1u, "a character is not a body: only the floor is one");
}
