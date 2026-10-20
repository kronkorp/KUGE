extern "C" {
    #include "kronklab/kronklab.h"
}
#include "physics3d_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // A player standing at `feet`: a head (a sphere, worth double), a body and legs (worth half)
    kw::Entity player(kw::World& world, Vec3 feet)
    {
        const kw::Entity entity = world.create();
        kuge::Transform3D transform;
        kuge::Hitboxes boxes;

        transform.translation = feet;
        boxes.parts.push_back({"head", kuge::Hitbox::Shape::Sphere, {0.15f, 0.0f, 0.0f}, {0.0f, 1.65f, 0.0f}, "", 2.0f});
        boxes.parts.push_back({"body", kuge::Hitbox::Shape::Capsule, {0.3f, 0.9f, 0.0f}, {0.0f, 1.0f, 0.0f}, "", 1.0f});
        boxes.parts.push_back({"legs", kuge::Hitbox::Shape::Box, {0.4f, 0.55f, 0.3f}, {0.0f, 0.275f, 0.0f}, "", 0.5f});
        world.add<kuge::Transform3D>(entity, transform);
        world.add<kuge::Hitboxes>(entity, boxes);
        return entity;
    }
}

Test(shoot, the_part_that_was_hit)
{
    kw::Entity target = 0;
    Sim sim([&](Level& level) { target = player(level.world(), {0.0f, 0.0f, -10.0f}); });

    sim.ticks(1);
    const auto head = sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.65f, 0.0f}, {0.0f, 0.0f, -1.0f}, 50.0f);
    const auto body = sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 50.0f);
    const auto legs = sim.physics().raycastHitboxes(sim.world(), {0.0f, 0.3f, 0.0f}, {0.0f, 0.0f, -1.0f}, 50.0f);

    Assert(head && head->entity == target && head->hitbox == 0 && head->weight == 2.0f, "the head");
    Assert(near(head->distance, 9.85f, 0.01f) && near(head->point.z, -9.85f, 0.01f) && head->normal.z > 0.9f, "10 m off, 15 cm of head: %f", head->distance);
    Assert(body && body->hitbox == 1 && body->weight == 1.0f && near(body->distance, 9.7f, 0.01f), "the body");
    Assert(legs && legs->hitbox == 2 && legs->weight == 0.5f && near(legs->distance, 9.85f, 0.01f), "the legs (a box)");
    Assert(!sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 50.0f), "behind the shooter: nothing");
    Assert(!sim.physics().raycastHitboxes(sim.world(), {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 50.0f), "a miss, by a meter");
}

Test(shoot, from_above_the_head_first)
{
    Sim sim([&](Level& level) { player(level.world(), {0.0f, 0.0f, -10.0f}); });

    sim.ticks(1);
    const auto hit = sim.physics().raycastHitboxes(sim.world(), {0.0f, 5.0f, -10.0f}, {0.0f, -1.0f, 0.0f}, 50.0f);

    Assert(hit && hit->hitbox == 0, "coming from above, the head is before the body");
    Assert(near(hit->point.y, 1.8f, 0.01f) && hit->normal.y > 0.9f, "on its top: y = %f", hit->point.y);
}

Test(shoot, a_wall_hides_the_target)
{
    kw::Entity wallEntity = 0, target = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();
        wallEntity = thing(w, {0.0f, 1.5f, -5.0f}, kuge::Collider3D::box(10.0f, 3.0f, 0.5f));
        target = player(w, {0.0f, 0.0f, -10.0f});
    });

    sim.ticks(1);
    const auto hit = sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 50.0f);

    Assert(hit && hit->entity == wallEntity && hit->hitbox == -1 && near(hit->distance, 4.75f, 0.01f), "the wall is hit, not the player behind it");
    const auto through = sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 50.0f, 0xFFFFFFFFu,
        [&](kw::Entity e) { return e == wallEntity; });

    Assert(through && through->entity == target, "unless the game lets shots through this wall (the filter)");
    const auto short_ = sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 3.0f);

    Assert(!short_, "the range is respected");
}

Test(shoot, far_and_inside)
{
    Sim sim([&](Level& level) { player(level.world(), {0.0f, 0.0f, -200.0f}); });

    sim.ticks(1);
    Assert(sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 300.0f), "a shot at 200 m");
    Assert(!sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.0f, -200.0f}, {0.0f, 0.0f, -1.0f}, 300.0f), "from inside the player: nothing (the shooter is not in his own way)");
}

Test(shoot, parts_follow_bones_and_turns)
{
    kw::Entity target = 0;
    Sim sim([&](Level& level) {
        target = player(level.world(), {0.0f, 0.0f, -10.0f});
        auto& boxes = level.world().get<kuge::Hitboxes>(target);

        boxes.parts[0].bone = "head";
        boxes.parts[0].offset = {};
        boxes.bones.push_back({"head", {0.0f, 1.65f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}});
    });

    sim.ticks(1);
    Assert(sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.65f, 0.0f}, {0.0f, 0.0f, -1.0f}, 50.0f)->hitbox == 0, "the head is where its bone is");
    sim.world().get<kuge::Hitboxes>(target).bones[0].translation = {0.5f, 1.65f, 0.0f};   // leans to the side: the animation moved it
    Assert(!sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.65f, 0.0f}, {0.0f, 0.0f, -1.0f}, 50.0f), "the bone moved: a shot at the old place misses");
    const auto leaned = sim.physics().raycastHitboxes(sim.world(), {0.5f, 1.65f, 0.0f}, {0.0f, 0.0f, -1.0f}, 50.0f);

    Assert(leaned && leaned->hitbox == 0, "and a shot at the new one hits the head");
    // The whole entity turned a quarter around y, and scaled: the offset turns and stretches with it
    sim.world().get<kuge::Hitboxes>(target).parts[0].bone.clear();
    sim.world().get<kuge::Hitboxes>(target).parts[0].offset = {-1.0f, 1.0f, 0.0f};
    sim.world().get<kuge::Transform3D>(target).rotation = kuge::axisAngle({0.0f, 1.0f, 0.0f}, 1.5707963f);
    const auto turned = sim.physics().raycastHitboxes(sim.world(), {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 50.0f);

    Assert(turned && turned->hitbox == 0, "an offset of -x, turned a quarter around y, is 1 m toward the shooter: the head is in front of the body: %d", turned ? turned->hitbox : -2);
}

Test(shoot, sweeping_a_capsule)
{
    kw::Entity wallEntity = 0;
    Sim sim([&](Level& level) {
        floor(level.world());
        wallEntity = thing(level.world(), {0.0f, 1.0f, -5.0f}, kuge::Collider3D::box(10.0f, 2.0f, 0.5f));
    });

    sim.ticks(1);
    const auto hit = sim.physics().sweepCapsule({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 20.0f, 0.4f, 1.8f);

    Assert(hit && hit->entity == wallEntity && near(hit->distance, 4.35f, 0.05f), "the capsule stops 0.4 m before the wall's face at z = -4.75: %f", hit ? hit->distance : -1.0f);
    Assert(hit->normal.z > 0.9f, "the wall's normal faces it");
    Assert(!sim.physics().sweepCapsule({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 3.0f, 0.4f, 1.8f), "free to 3 m");
    const auto down = sim.physics().sweepCapsule({0.0f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, 0.4f, 1.8f);

    Assert(down && near(down->distance, 2.1f, 0.05f), "it lands on the floor (y = 0): center at 0.9, so 2.1 m down: %f", down ? down->distance : -1.0f);
    const auto skipped = sim.physics().raycast({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 20.0f, 0xFFFFFFFFu, false, [&](kw::Entity e) { return e == wallEntity; });

    Assert(!skipped, "a ray that looks through the wall finds nothing behind it");
}
