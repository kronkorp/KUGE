extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Shapes.hpp"
#include "SolidGrid.hpp"
#include <cmath>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) < tolerance; }
    bool nearV(kuge::Vec2 a, kuge::Vec2 b) { return near(a.x, b.x) && near(a.y, b.y); }

    const kuge::Aabb BOX{{0.0f, 0.0f}, {10.0f, 10.0f}};
}

Test(shapes, boxes_overlap_or_touch)
{
    Assert(BOX.overlaps({{5.0f, 5.0f}, {15.0f, 15.0f}}), "overlapping");
    Assert(BOX.overlaps({{2.0f, 2.0f}, {3.0f, 3.0f}}), "one inside the other");
    Assert(!BOX.overlaps({{10.0f, 0.0f}, {20.0f, 10.0f}}), "sharing an edge is not overlapping");
    Assert(!BOX.overlaps({{10.0f, 10.0f}, {20.0f, 20.0f}}), "sharing a corner neither");
    Assert(!BOX.overlaps({{30.0f, 30.0f}, {40.0f, 40.0f}}), "far away");
    Assert(nearV(BOX.center(), {5.0f, 5.0f}) && nearV(BOX.half(), {5.0f, 5.0f}), "center and half size");
    Assert(nearV(kuge::Aabb::fromCenter({5.0f, 5.0f}, {5.0f, 5.0f}).max, {10.0f, 10.0f}), "from the center");
    Assert(nearV(BOX.expanded(1.0f).min, {-1.0f, -1.0f}), "expanded");
}

Test(shapes, circles)
{
    Assert(kuge::overlaps(BOX, kuge::Circle{{12.0f, 12.0f}, 5.0f}), "near a corner: 2.8 away, radius 5");
    Assert(!kuge::overlaps(BOX, kuge::Circle{{14.0f, 14.0f}, 5.0f}), "5.6 from the corner: no");
    Assert(kuge::overlaps(BOX, kuge::Circle{{5.0f, 5.0f}, 1.0f}), "inside the box");
    Assert(!kuge::overlaps(BOX, kuge::Circle{{15.0f, 5.0f}, 5.0f}), "touching a side only");
    Assert(kuge::overlaps(kuge::Circle{{0.0f, 0.0f}, 5.0f}, kuge::Circle{{8.0f, 0.0f}, 5.0f}), "circles that overlap");
    Assert(!kuge::overlaps(kuge::Circle{{0.0f, 0.0f}, 5.0f}, kuge::Circle{{10.0f, 0.0f}, 5.0f}), "circles that touch");
}

Test(shapes, ray_meets_a_box)
{
    auto hit = kuge::rayAabb({-10.0f, 5.0f}, {1.0f, 0.0f}, 100.0f, BOX);

    Assert(hit && near(hit->distance, 10.0f) && nearV(hit->normal, {-1.0f, 0.0f}), "from the left, on the left face");
    hit = kuge::rayAabb({20.0f, 5.0f}, {-1.0f, 0.0f}, 100.0f, BOX);
    Assert(hit && near(hit->distance, 10.0f) && nearV(hit->normal, {1.0f, 0.0f}), "from the right");
    hit = kuge::rayAabb({5.0f, -8.0f}, {0.0f, 1.0f}, 100.0f, BOX);
    Assert(hit && near(hit->distance, 8.0f) && nearV(hit->normal, {0.0f, -1.0f}), "from above (y points down)");
    hit = kuge::rayAabb({5.0f, 30.0f}, {0.0f, -1.0f}, 100.0f, BOX);
    Assert(hit && near(hit->distance, 20.0f) && nearV(hit->normal, {0.0f, 1.0f}), "from below");
    const float d = std::sqrt(0.5f);
    hit = kuge::rayAabb({-5.0f, -5.0f}, {d, d}, 100.0f, BOX);
    Assert(hit && near(hit->distance, 5.0f * std::sqrt(2.0f), 1e-3f), "diagonal, into the corner");
}

Test(shapes, ray_misses_a_box)
{
    Assert(!kuge::rayAabb({-10.0f, 20.0f}, {1.0f, 0.0f}, 100.0f, BOX).has_value(), "passes beside it");
    Assert(!kuge::rayAabb({-10.0f, 5.0f}, {1.0f, 0.0f}, 5.0f, BOX).has_value(), "too short to reach it");
    Assert(!kuge::rayAabb({-10.0f, 5.0f}, {-1.0f, 0.0f}, 100.0f, BOX).has_value(), "going away from it");
    Assert(!kuge::rayAabb({-10.0f, 15.0f}, {1.0f, 0.0f}, 100.0f, BOX).has_value(), "parallel, outside the slab");
    Assert(kuge::rayAabb({-10.0f, 5.0f}, {1.0f, 0.0f}, 10.0f, BOX).has_value(), "just long enough");
}

Test(shapes, ray_from_inside_a_box)
{
    const auto hit = kuge::rayAabb({5.0f, 5.0f}, {1.0f, 0.0f}, 100.0f, BOX);

    Assert(hit && hit->distance == 0.0f && hit->normal == kuge::Vec2(), "at once, with no normal");
}

Test(shapes, ray_meets_a_circle)
{
    const kuge::Circle circle{{0.0f, 0.0f}, 5.0f};
    auto hit = kuge::rayCircle({-10.0f, 0.0f}, {1.0f, 0.0f}, 100.0f, circle);

    Assert(hit && near(hit->distance, 5.0f) && nearV(hit->normal, {-1.0f, 0.0f}), "head on");
    hit = kuge::rayCircle({-10.0f, 3.0f}, {1.0f, 0.0f}, 100.0f, circle);
    Assert(hit && near(hit->distance, 10.0f - 4.0f) && hit->normal.x < 0.0f && hit->normal.y > 0.0f, "off center: the normal follows the surface");
    Assert(!kuge::rayCircle({-10.0f, 6.0f}, {1.0f, 0.0f}, 100.0f, circle).has_value(), "passes beside it");
    Assert(!kuge::rayCircle({10.0f, 0.0f}, {1.0f, 0.0f}, 100.0f, circle).has_value(), "it is behind");
    Assert(!kuge::rayCircle({-10.0f, 0.0f}, {1.0f, 0.0f}, 3.0f, circle).has_value(), "too short");
    hit = kuge::rayCircle({1.0f, 1.0f}, {1.0f, 0.0f}, 100.0f, circle);
    Assert(hit && hit->distance == 0.0f, "from inside: at once");
}

Test(shapes, push_out_of_a_circle)
{
    const kuge::Circle wall{{12.0f, 5.0f}, 5.0f};
    const auto right = kuge::pushOutAlongAxis(BOX, wall, 0, 1.0f);

    Assert(right && near(*right, -3.0f), "a box moving right: back by 3 (%f)", right ? *right : 0.0f);
    const auto left = kuge::pushOutAlongAxis(BOX, kuge::Circle{{-2.0f, 5.0f}, 5.0f}, 0, -1.0f);
    Assert(left && near(*left, 3.0f), "a box moving left: forward by 3");
    const auto down = kuge::pushOutAlongAxis(BOX, kuge::Circle{{5.0f, 12.0f}, 5.0f}, 1, 1.0f);
    Assert(down && near(*down, -3.0f), "and along y");
    Assert(!kuge::pushOutAlongAxis(BOX, kuge::Circle{{14.0f, 14.0f}, 5.0f}, 0, 1.0f).has_value(), "no overlap: nothing to do");

    // Off center, the circle is narrower there: a smaller push
    const kuge::Circle round{{12.0f, 12.0f}, 5.0f};
    const auto off = kuge::pushOutAlongAxis(BOX, round, 0, 1.0f);
    Assert(off && *off > -3.0f && *off < 0.0f, "less than at the center: %f", off ? *off : 0.0f);
    kuge::Aabb moved = BOX;
    moved.min.x += *off - 0.001f;
    moved.max.x += *off - 0.001f;
    Assert(!kuge::overlaps(moved, round), "and it is enough to get out");
}

Test(shapes, solid_grid)
{
    kuge::SolidGrid grid;

    Assert(grid.empty(), "empty at first");
    grid.reset(4, 3, 16.0f, {100.0f, 50.0f});
    Assert(!grid.empty(), "not any more");
    grid.set(1, 2, true);
    grid.set(9, 9, true);      // out of the level: ignored
    grid.set(-1, 0, true);
    Assert(grid.isSolid(1, 2), "solid");
    Assert(!grid.isSolid(0, 0) && !grid.isSolid(9, 9) && !grid.isSolid(-1, 0), "the rest is not, and outside is not");
    Assert(nearV(grid.tile(1, 2).min, {116.0f, 82.0f}) && nearV(grid.tile(1, 2).max, {132.0f, 98.0f}), "where a tile is");
    int x, y;
    grid.tileAt({117.0f, 83.0f}, x, y);
    Assert(x == 1 && y == 2, "which tile a point is in");
    grid.tileAt({99.0f, 49.0f}, x, y);
    Assert(x == -1 && y == -1, "before the origin: negative, not 0 (%d, %d)", x, y);
    Assert(grid.isSolidAt({120.0f, 90.0f}) && !grid.isSolidAt({120.0f, 60.0f}), "solid at a point");
    grid.set(1, 2, false);
    Assert(!grid.isSolid(1, 2), "and not any more");
}
