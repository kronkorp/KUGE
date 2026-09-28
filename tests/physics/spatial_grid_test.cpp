extern "C" {
    #include "kronklab/kronklab.h"
}
#include "SpatialGrid.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // A generator that gives the same numbers everywhere
    struct Random
    {
        std::uint32_t state;
        float next(float low, float high)
        {
            state = state * 1664525u + 1013904223u;
            return low + (high - low) * static_cast<float>(state >> 8) / static_cast<float>(1 << 24);
        }
    };

    bool contains(const std::vector<std::uint32_t>& items, std::uint32_t item)
    {
        return std::find(items.begin(), items.end(), item) != items.end();
    }
}

Test(grid, finds_what_is_near)
{
    kuge::SpatialGrid grid(10.0f);
    std::vector<std::uint32_t> found;

    grid.insert(0, {{0.0f, 0.0f}, {5.0f, 5.0f}});
    grid.insert(1, {{100.0f, 100.0f}, {105.0f, 105.0f}});
    grid.insert(2, {{3.0f, 3.0f}, {8.0f, 8.0f}});
    grid.finish();
    grid.query({{1.0f, 1.0f}, {4.0f, 4.0f}}, found);
    Assert(contains(found, 0) && contains(found, 2), "the two near ones");
    Assert(!contains(found, 1), "and not the one far away");
}

Test(grid, answers_are_sorted_once)
{
    kuge::SpatialGrid grid(10.0f);
    std::vector<std::uint32_t> found;

    // Several cells each: they would be found several times
    grid.insert(7, {{0.0f, 0.0f}, {35.0f, 35.0f}});
    grid.insert(3, {{0.0f, 0.0f}, {35.0f, 35.0f}});
    grid.insert(5, {{10.0f, 10.0f}, {20.0f, 20.0f}});
    grid.finish();
    grid.query({{0.0f, 0.0f}, {40.0f, 40.0f}}, found);
    AssertEq(found.size(), 3, "three items, once each, got %zu", found.size());
    Assert(std::is_sorted(found.begin(), found.end()), "in increasing order");
}

Test(grid, never_misses_anything)
{
    kuge::SpatialGrid grid(16.0f);
    Random random{12345};
    std::vector<kuge::Aabb> boxes;
    std::vector<std::uint32_t> found;

    for (std::uint32_t i = 0; i < 300; ++i) {
        const float x = random.next(-500.0f, 500.0f);
        const float y = random.next(-500.0f, 500.0f);
        const kuge::Aabb box{{x, y}, {x + random.next(1.0f, 60.0f), y + random.next(1.0f, 60.0f)}};

        boxes.push_back(box);
        grid.insert(i, box);
    }
    grid.finish();
    for (int q = 0; q < 200; ++q) {
        const float x = random.next(-500.0f, 500.0f);
        const float y = random.next(-500.0f, 500.0f);
        const kuge::Aabb area{{x, y}, {x + random.next(1.0f, 80.0f), y + random.next(1.0f, 80.0f)}};

        grid.query(area, found);
        Assert(std::is_sorted(found.begin(), found.end()) && std::adjacent_find(found.begin(), found.end()) == found.end(),
            "sorted and once each");
        for (std::uint32_t i = 0; i < boxes.size(); ++i) {
            Assert(!boxes[i].overlaps(area) || contains(found, i), "query %d missed the item %u that overlaps it", q, i);
        }
    }
}

Test(grid, large_items_are_always_found)
{
    kuge::SpatialGrid grid(10.0f);
    std::vector<std::uint32_t> found;

    grid.insert(0, {{-10000.0f, 0.0f}, {10000.0f, 20.0f}});   // the floor of a level
    grid.insert(1, {{0.0f, 0.0f}, {5.0f, 5.0f}});
    grid.finish();
    grid.query({{4000.0f, -5.0f}, {4010.0f, 5.0f}}, found);
    Assert(contains(found, 0), "found from anywhere along it");
    Assert(!contains(found, 1), "and the small one is not");
}

Test(grid, any_coordinates)
{
    kuge::SpatialGrid grid(32.0f);
    std::vector<std::uint32_t> found;

    grid.insert(0, {{-5000.0f, -5000.0f}, {-4990.0f, -4990.0f}});
    grid.insert(1, {{0.0f, 0.0f}, {1.0f, 1.0f}});
    grid.insert(2, {{1.0e7f, 1.0e7f}, {1.0e7f + 10.0f, 1.0e7f + 10.0f}});
    grid.finish();
    grid.query({{-5001.0f, -5001.0f}, {-4995.0f, -4995.0f}}, found);
    Assert(contains(found, 0) && !contains(found, 1), "far in the negatives");
    grid.query({{1.0e7f, 1.0e7f}, {1.0e7f + 5.0f, 1.0e7f + 5.0f}}, found);
    Assert(contains(found, 2), "and far in the positives");
    grid.query({{-0.5f, -0.5f}, {0.5f, 0.5f}}, found);
    Assert(contains(found, 1), "around the origin");
}

Test(grid, a_huge_query)
{
    kuge::SpatialGrid grid(1.0f);
    std::vector<std::uint32_t> found;

    for (std::uint32_t i = 0; i < 20; ++i) {
        grid.insert(i, {{static_cast<float>(i) * 10.0f, 0.0f}, {static_cast<float>(i) * 10.0f + 1.0f, 1.0f}});
    }
    grid.finish();
    grid.query({{-1.0e6f, -1.0e6f}, {1.0e6f, 1.0e6f}}, found);
    AssertEq(found.size(), 20, "everything, without visiting a million cells");
}

Test(grid, clear_forgets)
{
    kuge::SpatialGrid grid(10.0f);
    std::vector<std::uint32_t> found;

    grid.insert(0, {{0.0f, 0.0f}, {5.0f, 5.0f}});
    grid.finish();
    grid.clear();
    grid.finish();
    grid.query({{0.0f, 0.0f}, {5.0f, 5.0f}}, found);
    AssertEq(found.size(), 0, "nothing left");
    grid.setCellSize(4.0f);
    AssertEq(grid.cellSize(), 4.0f, "the cell size can change");
}
