extern "C" {
    #include "kronklab/kronklab.h"
}
#include "physics_fixture.hpp"
#include <algorithm>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using kuge::Collider;
    using kuge::Vec2;
}

Test(queries, overlap_a_rectangle)
{
    kw::Entity a = 0, b = 0, c = 0, zone = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();

        a = wall(w, {0.0f, 0.0f}, Collider::box(20.0f, 20.0f));
        b = wall(w, {30.0f, 0.0f}, Collider::circle(8.0f).onLayer(2));
        c = wall(w, {500.0f, 500.0f}, Collider::box(20.0f, 20.0f));
        zone = wall(w, {10.0f, 10.0f}, Collider::box(10.0f, 10.0f).asTrigger());
    });

    sim.ticks(1);
    auto found = sim.physics().overlapRect({-5.0f, -5.0f, 40.0f, 20.0f});
    Assert(found == std::vector<kw::Entity>({a, b}), "what is in the area, by entity, without the trigger (got %zu)", found.size());
    found = sim.physics().overlapRect({-5.0f, -5.0f, 40.0f, 20.0f}, 0xFFFFFFFFu, true);
    AssertEq(found.size(), 3, "with the triggers if asked");
    found = sim.physics().overlapRect({-5.0f, -5.0f, 40.0f, 20.0f}, 2);
    Assert(found == std::vector<kw::Entity>({b}), "only the layers asked for");
    AssertEq(sim.physics().overlapRect({200.0f, 200.0f, 10.0f, 10.0f}).size(), 0, "nothing there");
    (void)c;
}

Test(queries, overlap_a_circle)
{
    kw::Entity box = 0, ball = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();

        box = wall(w, {30.0f, 0.0f}, Collider::box(20.0f, 20.0f));      // from x = 20
        ball = wall(w, {0.0f, 30.0f}, Collider::circle(5.0f));
        wall(w, {200.0f, 0.0f}, Collider::box(20.0f, 20.0f));
    });

    sim.ticks(1);
    // The box is 20 away at its nearest (its left side is at x = 20), the ball 30 - 5 = 25
    auto found = sim.physics().overlapCircle({0.0f, 0.0f}, 22.0f);
    Assert(found == std::vector<kw::Entity>({box}), "radius 22 reaches the box only, got %zu", found.size());
    found = sim.physics().overlapCircle({0.0f, 0.0f}, 26.0f);
    Assert(found == std::vector<kw::Entity>({box, ball}), "radius 26 reaches both, by entity, got %zu", found.size());
    AssertEq(sim.physics().overlapCircle({0.0f, 0.0f}, 10.0f).size(), 0, "radius 10 reaches nothing");
}

Test(queries, ray_finds_the_nearest)
{
    kw::Entity near1 = 0, far1 = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();

        far1 = wall(w, {200.0f, 0.0f}, Collider::box(20.0f, 20.0f));
        near1 = wall(w, {100.0f, 0.0f}, Collider::circle(10.0f));
    });

    sim.ticks(1);
    auto hit = sim.physics().raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 1000.0f);
    Assert(hit && hit->entity == near1 && !hit->tile, "the circle, first");
    Assert(hit && near(hit->distance, 90.0f) && near(hit->point.x, 90.0f) && near(hit->normal.x, -1.0f), "where and how");
    hit = sim.physics().raycast({0.0f, 0.0f}, {5.0f, 0.0f}, 1000.0f);
    Assert(hit && near(hit->distance, 90.0f), "the direction can be any length");
    hit = sim.physics().raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 50.0f);
    Assert(!hit.has_value(), "too short");
    hit = sim.physics().raycast({300.0f, 0.0f}, {-1.0f, 0.0f}, 1000.0f);
    Assert(hit && hit->entity == far1 && near(hit->distance, 90.0f), "from the other side, the other one");
    Assert(!sim.physics().raycast({0.0f, 100.0f}, {1.0f, 0.0f}, 1000.0f).has_value(), "beside them");
    Assert(!sim.physics().raycast({0.0f, 0.0f}, {0.0f, 0.0f}, 1000.0f).has_value(), "no direction, no ray");
}

Test(queries, ray_layers_and_triggers)
{
    kw::Entity solid = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();

        wall(w, {50.0f, 0.0f}, Collider::box(10.0f, 10.0f).asTrigger());
        wall(w, {100.0f, 0.0f}, Collider::box(10.0f, 10.0f).onLayer(2));
        solid = wall(w, {150.0f, 0.0f}, Collider::box(10.0f, 10.0f).onLayer(1));
    });

    sim.ticks(1);
    // From the left: a trigger (left face at 45), a box on layer 2 (95), a box on layer 1 (145)
    auto hit = sim.physics().raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 1000.0f);
    Assert(hit && near(hit->distance, 95.0f), "a trigger does not stop a ray: the first is at 95 (got %f)", hit ? hit->distance : -1.0f);
    hit = sim.physics().raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 1000.0f, 1);
    Assert(hit && hit->entity == solid && near(hit->distance, 145.0f), "only layer 1: it skips the layer 2 box");
    hit = sim.physics().raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 1000.0f, 0xFFFFFFFFu, true);
    Assert(hit && near(hit->distance, 45.0f), "triggers are met when asked for");
}

Test(queries, ray_meets_tiles)
{
    Sim sim([](Level& level) {
        auto& tiles = level.world().getResource<kuge::Physics2D>().tiles;

        tiles.reset(20, 10, 16.0f);
        for (int y = 0; y < 10; ++y) {
            tiles.set(10, y, true);                      // a wall, its left face is at x = 160
        }
        tiles.set(3, 5, true);                           // a block: x 48..64, y 80..96
    });

    sim.ticks(1);
    auto hit = sim.physics().raycast({8.0f, 24.0f}, {1.0f, 0.0f}, 1000.0f);
    Assert(hit && hit->tile && near(hit->distance, 152.0f) && near(hit->normal.x, -1.0f) && near(hit->normal.y, 0.0f), "the wall of tiles, on its left face");
    hit = sim.physics().raycast({56.0f, 8.0f}, {0.0f, 1.0f}, 1000.0f);
    Assert(hit && hit->tile && near(hit->distance, 72.0f) && near(hit->normal.y, -1.0f), "the block, from above");
    hit = sim.physics().raycast({-100.0f, 24.0f}, {1.0f, 0.0f}, 1000.0f);
    Assert(hit && hit->tile && near(hit->distance, 260.0f), "from outside the level: it enters, then hits");
    Assert(!sim.physics().raycast({-100.0f, 500.0f}, {1.0f, 0.0f}, 1000.0f).has_value(), "and passes beside the level");
    hit = sim.physics().raycast({168.0f, 24.0f}, {-1.0f, 0.0f}, 1000.0f);
    Assert(hit && hit->tile && near(hit->distance, 0.0f), "from inside a solid tile: at once");
    hit = sim.physics().raycast({8.0f, 24.0f}, {1.0f, 0.0f}, 100.0f);
    Assert(!hit.has_value(), "too short to reach");
    hit = sim.physics().raycast({8.0f, 8.0f}, {2.0f, 1.0f}, 1000.0f);
    Assert(hit && hit->tile && near(hit->distance, std::sqrt(152.0f * 152.0f + 76.0f * 76.0f), 0.5f) && near(hit->normal.x, -1.0f),
        "a diagonal ray reaches the wall on its left face (got %f)", hit ? hit->distance : -1.0f);
    Assert(sim.physics().tiles.isSolidAt({170.0f, 10.0f}) && !sim.physics().tiles.isSolidAt({10.0f, 10.0f}), "solid at a point");
}

Test(queries, entity_before_tile_when_equal)
{
    kw::Entity box = 0;
    Sim sim([&box](Level& level) {
        auto& tiles = level.world().getResource<kuge::Physics2D>().tiles;

        tiles.reset(20, 10, 16.0f);
        tiles.set(10, 0, true);                                   // its left face is at x = 160
        box = wall(level.world(), {170.0f, 8.0f}, Collider::box(20.0f, 16.0f));   // left face at 160 too
    });

    sim.ticks(1);
    const auto hit = sim.physics().raycast({8.0f, 8.0f}, {1.0f, 0.0f}, 1000.0f);

    Assert(hit && !hit->tile && hit->entity == box, "same distance: the entity wins, always");
}

Test(queries, nothing_before_the_first_tick)
{
    Sim sim([](Level& level) { wall(level.world(), {50.0f, 0.0f}, Collider::box(10.0f, 10.0f)); });

    // The questions describe the world as the last tick left it: none has run yet
    Assert(!sim.physics().raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 1000.0f).has_value(), "no ray hits anything yet");
    AssertEq(sim.physics().overlapRect({0.0f, 0.0f, 100.0f, 100.0f}).size(), 0, "nothing overlaps yet");
    sim.ticks(1);
    Assert(sim.physics().raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 1000.0f).has_value(), "after a tick it does");
    AssertEq(sim.physics().overlapRect({0.0f, 0.0f, 100.0f, 100.0f}).size(), 1, "and it overlaps");
}
