extern "C" {
    #include "kronklab/kronklab.h"
}
#include "TilemapCollision.hpp"
#include "physics_fixture.hpp"

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // Tile 1 and 2 are solid, 3 is scenery
    const char* LEVEL = R"(kuge-tilemap 1
size 8 5
tilesize 16
solid 1 2
layer ground
0 0 0 0 0 0 0 0
0 0 0 0 0 0 0 0
0 0 0 3 3 0 0 2
0 0 0 0 0 0 0 2
1 1 1 1 1 1 1 1
layer decoration
3 3 3 3 3 3 3 3
0 0 0 0 0 0 0 0
0 0 0 0 0 0 0 0
0 0 0 0 0 0 0 0
0 0 0 0 0 0 0 0
)";
}

Test(tilecollision, solids_come_from_the_map)
{
    const auto map = kuge::TileMap::parse(LEVEL);
    const auto grid = kuge::makeSolidGrid(map, "ground");

    AssertEq(grid.width, 8, "width");
    AssertEq(grid.height, 5, "height");
    AssertEq(grid.tileSize, 16.0f, "tile size");
    Assert(grid.isSolid(0, 4) && grid.isSolid(7, 4), "the floor");
    Assert(grid.isSolid(7, 2) && grid.isSolid(7, 3), "the wall");
    Assert(!grid.isSolid(3, 2) && !grid.isSolid(4, 2), "scenery (tile 3) is not solid");
    Assert(!grid.isSolid(0, 0), "empty is not");
    Assert(!kuge::makeSolidGrid(map, "decoration").isSolid(0, 0), "another layer: its own tiles, and 3 is scenery there too");
}

Test(tilecollision, origin_and_layers)
{
    const auto map = kuge::TileMap::parse(LEVEL);
    const auto moved = kuge::makeSolidGrid(map, "ground", {100.0f, 200.0f});
    bool missing = false;

    Assert(moved.isSolidAt({100.0f + 8.0f, 200.0f + 4 * 16.0f + 8.0f}), "the floor follows the origin");
    Assert(!moved.isSolidAt({8.0f, 4 * 16.0f + 8.0f}), "and is not where it would be at (0, 0)");
    try { kuge::makeSolidGrid(map, "nothing"); } catch (const kuge::TileMapError&) { missing = true; }
    Assert(missing, "a layer that is not there");
}

Test(tilecollision, a_body_lands_on_it)
{
    const auto map = kuge::TileMap::parse(LEVEL);
    kw::Entity body = 0, walker = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();

        w.getResource<kuge::Physics2D>().tiles = kuge::makeSolidGrid(map, "ground");
        body = mover(w, {20.0f, 0.0f}, kuge::Collider::box(10.0f, 10.0f), dynamic());
        // Walking right along the scenery, at the height of the floor, to the wall
        walker = mover(w, {20.0f, 64.0f - 5.0f}, kuge::Collider::box(10.0f, 10.0f), dynamic({200.0f, 0.0f}));
    });

    sim.ticks(120);
    Assert(near(sim.where(body).y, 64.0f - 5.0f, 0.01f), "it stands on the floor tiles (top at 64): %f", sim.where(body).y);
    Assert(sim.body(body).contacts.down, "standing");
    Assert(near(sim.where(walker).x, 7 * 16.0f - 5.0f, 0.01f), "the walker stops at the wall (left face at 112): %f", sim.where(walker).x);
    Assert(sim.body(walker).contacts.right, "against it");
}

Test(tilecollision, scenery_lets_pass)
{
    const auto map = kuge::TileMap::parse(LEVEL);
    kw::Entity body = 0;
    Sim sim([&](Level& level) {
        auto& w = level.world();

        w.getResource<kuge::Physics2D>().tiles = kuge::makeSolidGrid(map, "ground");
        // Tile 3 is at (3..4, 2): a body walking through it is not stopped
        body = mover(w, {2.0f, 2 * 16.0f + 8.0f}, kuge::Collider::box(6.0f, 6.0f), kinematic({150.0f, 0.0f}));
    });

    sim.ticks(30);
    Assert(sim.where(body).x > 60.0f, "went through the scenery: %f", sim.where(body).x);
}
