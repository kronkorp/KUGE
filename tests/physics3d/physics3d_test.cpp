extern "C" {
    #include "kronklab/kronklab.h"
}

// NOTE: kronklab test names are limited to 31 characters.

#include "Level.hpp"
#include "physics3d_fixture.hpp"

Test(physics3d, a_crate_lands)
{
    kw::Entity box = 0;
    Sim sim([&box](Level& level) { floor(level.world()); box = crate(level.world(), {0.0f, 3.0f, 0.0f}); });

    sim.ticks(10);
    Assert(sim.where(box).y < 3.0f && !sim.body(box).grounded, "it falls");
    sim.ticks(170);
    Assert(near(sim.where(box).y, 0.5f), "it rests on the floor: y = %f", sim.where(box).y);
    Assert(sim.body(box).grounded, "and knows it");
    Assert(near(sim.body(box).velocity.y, 0.0f, 0.1f), "still");
    AssertEq(sim.physics().bodyCount(), 2u, "two bodies for two entities");
}

Test(physics3d, gravity_is_a_setting)
{
    kw::Entity box = 0;
    Sim sim([&box](Level& level) {
        level.world().getResource<kuge::Physics3D>();
        box = crate(level.world(), {0.0f, 0.0f, 0.0f});
    });

    sim.ticks(60);
    // One second of free fall: 0.5 * g * t^2 (a little more, a tick at a time)
    Assert(near(sim.where(box).y, -0.5f * 9.81f, 0.3f), "fell %f", sim.where(box).y);
    sim.world().get<kuge::Body3D>(box).gravityScale = 0.0f;
    sim.world().get<kuge::Body3D>(box).velocity = {};
    const float y = sim.where(box).y;

    sim.ticks(30);
    Assert(near(sim.where(box).y, y, 0.01f), "no gravity, no speed: it stays");
}

Test(physics3d, kinematic_moves)
{
    kw::Entity platform = 0;
    kuge::Body3D body;

    body.type = kuge::Body3D::Type::Kinematic;
    body.velocity = {2.0f, 0.0f, 0.0f};
    Sim sim([&](Level& level) { platform = crate(level.world(), {0.0f, 1.0f, 0.0f}, kuge::Collider3D::box(2.0f, 0.5f, 2.0f), body); });

    sim.ticks(60);
    Assert(near(sim.where(platform).x, 2.0f, 0.1f) && near(sim.where(platform).y, 1.0f, 0.01f), "2 m in a second, no gravity: x = %f", sim.where(platform).x);
}

Test(physics3d, the_game_can_teleport_and_push)
{
    kw::Entity box = 0;
    Sim sim([&box](Level& level) { floor(level.world()); box = crate(level.world(), {0.0f, 0.5f, 0.0f}); });

    sim.ticks(30);
    sim.world().get<kuge::Transform3D>(box).translation = {10.0f, 0.5f, -4.0f};
    sim.ticks(1);
    Assert(near(sim.where(box).x, 10.0f, 0.01f) && near(sim.where(box).z, -4.0f, 0.01f), "it is where the game put it: %f %f", sim.where(box).x, sim.where(box).z);
    sim.body(box).velocity.x = 3.0f;
    sim.ticks(30);
    Assert(sim.where(box).x > 10.5f, "and moves by the speed the game gave: x = %f", sim.where(box).x);
}

Test(physics3d, triggers_report)
{
    kw::Entity zone = 0;
    kw::Entity box = 0;
    Sim sim([&](Level& level) {
        zone = thing(level.world(), {0.0f, 0.0f, 0.0f}, kuge::Collider3D::box(4.0f, 2.0f, 4.0f).asTrigger());
        box = crate(level.world(), {0.0f, 4.0f, 0.0f});
    });
    int entered = 0;
    int left = 0;

    for (int t = 0; t < 120; ++t) {
        sim.ticks(1);
        for (const auto& event : sim.physics().events()) {
            Assert(event.trigger == zone && event.other == box, "who");
            entered += event.entered;
            left += !event.entered;
        }
    }
    AssertEq(entered, 1, "it went in once");
    AssertEq(left, 1, "and out once: it is not stopped by the zone");
    Assert(sim.where(box).y < -3.0f, "it fell through: y = %f", sim.where(box).y);
}

Test(physics3d, layers_choose)
{
    kw::Entity ghost = 0;
    kw::Entity solid = 0;
    Sim sim([&](Level& level) {
        floor(level.world(), kuge::Collider3D::box(50.0f, 1.0f, 50.0f).onLayer(0b01));
        ghost = crate(level.world(), {-3.0f, 2.0f, 0.0f}, kuge::Collider3D::box(1.0f, 1.0f, 1.0f).onLayer(0b10, 0b10));   // meets only layer 2
        solid = crate(level.world(), {3.0f, 2.0f, 0.0f});
    });

    sim.ticks(180);
    Assert(near(sim.where(solid).y, 0.5f), "this one meets the floor");
    Assert(sim.where(ghost).y < -1.0f, "this one does not: y = %f", sim.where(ghost).y);
}

Test(physics3d, ray_and_overlap)
{
    kw::Entity ground = 0;
    kw::Entity box = 0;
    kw::Entity zone = 0;
    Sim sim([&](Level& level) {
        ground = floor(level.world());
        box = crate(level.world(), {5.0f, 0.5f, 0.0f});
        zone = thing(level.world(), {5.0f, 0.5f, 0.0f}, kuge::Collider3D::sphere(1.0f).asTrigger());
    });

    sim.ticks(30);
    const auto down = sim.physics().raycast({0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f);

    Assert(down && down->entity == ground, "a ray down hits the floor");
    Assert(near(down->distance, 10.0f, 0.01f) && near(down->point.y, 0.0f, 0.01f) && near(down->normal.y, 1.0f, 0.01f), "at 10 m, on its top: %f %f", down->distance, down->normal.y);
    Assert(!sim.physics().raycast({0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 5.0f), "too short");
    Assert(!sim.physics().raycast({0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f, 0b10), "a mask that excludes the floor's layer (1)");
    const auto across = sim.physics().raycast({0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f);

    Assert(across && across->entity == box && near(across->distance, 4.5f, 0.05f), "a ray across hits the crate, not the zone around it");
    const auto into = sim.physics().raycast({0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f, 0xFFFFFFFFu, true);

    Assert(into && into->entity == zone, "unless triggers are asked for");
    const auto around = sim.physics().overlapSphere({5.0f, 0.5f, 0.0f}, 0.5f);

    Assert(around.size() == 1 && around[0] == box, "a sphere on the crate finds it, and not the zone");
    AssertEq(sim.physics().overlapSphere({5.0f, 0.5f, 0.0f}, 0.5f, 0xFFFFFFFFu, true).size(), 2u, "both with triggers");
    Assert(sim.physics().overlapSphere({-20.0f, 5.0f, 0.0f}, 1.0f).empty(), "nothing there");
}

Test(physics3d, bodies_follow_entities)
{
    kw::Entity box = 0;
    Sim sim([&box](Level& level) { floor(level.world()); box = crate(level.world(), {0.0f, 3.0f, 0.0f}); });

    sim.ticks(5);
    AssertEq(sim.physics().bodyCount(), 2u, "two");
    sim.world().remove(box);
    sim.ticks(1);
    AssertEq(sim.physics().bodyCount(), 1u, "the crate is gone with its entity");
    box = crate(sim.world(), {0.0f, 3.0f, 0.0f});
    sim.ticks(1);
    AssertEq(sim.physics().bodyCount(), 2u, "and a new one makes a body");
    sim.world().remove<kuge::Collider3D>(box);
    sim.ticks(1);
    AssertEq(sim.physics().bodyCount(), 1u, "so does losing the collider");
}

Test(physics3d, characters_stay_upright)
{
    kw::Entity walker = 0;
    kuge::Body3D body;

    body.lockRotation = true;
    Sim sim([&](Level& level) {
        floor(level.world());
        walker = crate(level.world(), {0.0f, 1.0f, 0.0f}, kuge::Collider3D::capsule(0.4f, 1.8f), body);
        level.world().add<kuge::Transform3D>(level.world().create(), kuge::Transform3D{});   // (an entity that is no body)
    });

    sim.ticks(60);
    Assert(sim.body(walker).grounded, "standing");
    for (int t = 0; t < 120; ++t) {
        sim.body(walker).velocity.x = 2.0f;     // the game walks it, a tick at a time
        sim.ticks(1);
    }
    const auto& rotation = sim.world().get<kuge::Transform3D>(walker).rotation;

    Assert(sim.where(walker).x > 3.0f, "walked: x = %f", sim.where(walker).x);
    Assert(near(std::fabs(rotation.w), 1.0f, 1e-4f) && near(rotation.x, 0.0f, 1e-4f) && near(rotation.z, 0.0f, 1e-4f), "and did not fall over");
    Assert(near(sim.where(walker).y, 0.9f, 0.05f), "on its feet: y = %f", sim.where(walker).y);
}

Test(physics3d, a_character_falls_along_a_wall)
{
    kw::Entity walker = 0;
    kuge::Body3D body;

    body.lockRotation = true;
    Sim sim([&](Level& level) {
        floor(level.world());
        thing(level.world(), {3.0f, 5.0f, 0.0f}, kuge::Collider3D::box(1.0f, 10.0f, 10.0f));     // a wall
        walker = crate(level.world(), {2.0f, 8.0f, 0.0f}, kuge::Collider3D::capsule(0.4f, 1.8f), body);
    });

    // In the air, pushing against the wall (the game sets the velocity at each tick, as a walking player does)
    for (int t = 0; t < 45; ++t) {
        sim.body(walker).velocity.x = 4.0f;
        sim.ticks(1);
    }
    Assert(near(sim.where(walker).x, 2.1f, 0.1f), "it is against the wall: x = %f", sim.where(walker).x);
    Assert(sim.where(walker).y < 8.0f - 2.0f, "and it falls, the wall does not hold it: y = %f", sim.where(walker).y);
    for (int t = 0; t < 60; ++t) {
        sim.body(walker).velocity.x = 4.0f;
        sim.ticks(1);
    }
    Assert(near(sim.where(walker).y, 0.9f, 0.1f) && sim.body(walker).grounded, "down on the floor: y = %f", sim.where(walker).y);
}

Test(physics3d, same_world_same_result)
{
    const auto run = []() {
        std::vector<Vec3> end;
        Sim sim([](Level& level) {
            floor(level.world());
            for (int i = 0; i < 12; ++i) {
                crate(level.world(), {static_cast<float>(i % 3) * 0.6f, 1.0f + static_cast<float>(i) * 1.1f, static_cast<float>(i % 2) * 0.4f});
            }
        });

        sim.ticks(300);
        for (kw::Entity e : sim.world().view<kuge::Body3D>()) {
            end.push_back(sim.where(e));
        }
        std::sort(end.begin(), end.end(), [](const Vec3& a, const Vec3& b) { return a.x != b.x ? a.x < b.x : (a.y != b.y ? a.y < b.y : a.z < b.z); });
        return end;
    };
    const auto first = run();
    const auto second = run();

    AssertEq(first.size(), 12u, "twelve crates");
    bool same = first.size() == second.size();

    for (std::size_t i = 0; same && i < first.size(); ++i) {
        same = first[i].x == second[i].x && first[i].y == second[i].y && first[i].z == second[i].z;
    }
    Assert(same, "the same crates in the same places, to the last bit");
}

namespace
{
    // A slope that rises 10 m over 20 m (a roof), as two triangles whose front is up: y = 5 at x = 0
    std::shared_ptr<const kuge::CollisionMesh> slope(void)
    {
        auto mesh = std::make_shared<kuge::CollisionMesh>();

        mesh->vertices = {{-10.0f, 0.0f, 5.0f}, {10.0f, 10.0f, 5.0f}, {10.0f, 10.0f, -5.0f}, {-10.0f, 0.0f, -5.0f}};
        mesh->indices = {0, 1, 2, 0, 2, 3};
        return mesh;
    }
}

Test(physics3d, a_ray_stops_on_a_mesh)
{
    Sim sim([](Level& level) { thing(level.world(), {}, kuge::Collider3D::triangles(slope())); });

    sim.ticks(1);
    const auto hit = sim.physics().raycast({0.0f, 20.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 40.0f);

    Assert(hit.has_value(), "the ray hits the slope");
    Assert(near(hit->point.y, 5.0f, 0.01f), "on the slope (5 m) and not on a box over it (10 m): y = %f", hit->point.y);
    Assert(hit->normal.y > 0.8f && hit->normal.y < 0.99f && hit->normal.x < -0.3f, "with its normal: (%f, %f)", hit->normal.x, hit->normal.y);
    Assert(!sim.physics().raycast({0.0f, 20.0f, 8.0f}, {0.0f, -1.0f, 0.0f}, 40.0f).has_value(), "and the air beside it is the air");
}

Test(physics3d, a_crate_slides_down_a_mesh)
{
    kw::Entity box = 0;
    kuge::Collider3D ice = kuge::Collider3D::triangles(slope());

    ice.friction = 0.05f;
    Sim sim([&](Level& level) {
        thing(level.world(), {}, ice);
        box = crate(level.world(), {0.0f, 6.5f, 0.0f}, kuge::Collider3D::box(1.0f, 1.0f, 1.0f).onLayer(1));
        level.world().get<kuge::Collider3D>(box).friction = 0.05f;
    });

    sim.ticks(150);
    Assert(sim.where(box).x < -2.0f, "it slides to the low end: x = %f", sim.where(box).x);
    Assert(sim.where(box).y < 5.0f && sim.where(box).y > -1.0f, "on the slope, not through it: y = %f", sim.where(box).y);
}

Test(physics3d, a_mesh_does_not_fall)
{
    kw::Entity roof = 0;
    Sim sim([&](Level& level) { roof = crate(level.world(), {}, kuge::Collider3D::triangles(slope())); });

    sim.ticks(60);
    Assert(near(sim.where(roof).y, 0.0f, 0.001f), "a mesh with a dynamic body is made static: y = %f", sim.where(roof).y);
}

Test(physics3d, what_stood_on_a_piece_falls)
{
    kw::Entity box = 0;
    Sim sim([&box](Level& level) {
        const kw::Entity piece = floor(level.world());

        level.world().add<kuge::LevelPiece>(piece, kuge::LevelPiece{0});
        box = crate(level.world(), {0.0f, 0.5f, 0.0f});
    });

    sim.ticks(300);   // (long enough for it to sleep on the floor)
    Assert(near(sim.where(box).y, 0.5f), "it rests: y = %f", sim.where(box).y);
    kuge::LevelBreaks breaks;

    breaks.add(0);
    AssertEq(kuge::applyBreaks(sim.world(), breaks), 1u, "the piece breaks");
    sim.ticks(60);
    Assert(sim.where(box).y < -3.0f, "and the crate that slept on it falls: y = %f", sim.where(box).y);
    AssertEq(sim.physics().bodyCount(), 1u, "only the crate is left");
}
