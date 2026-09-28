#include "SpatialGrid.hpp"
#include <algorithm>
#include <cmath>

namespace
{
    // Cell coordinates are kept far from the limits of an int, and shifted to be
    // positive so that the keys sort like the cells do
    constexpr int LIMIT = 1 << 29;
    constexpr std::int64_t BIAS = 1 << 30;

    std::uint64_t keyOf(int x, int y)
    {
        return (static_cast<std::uint64_t>(y + BIAS) << 32) | static_cast<std::uint64_t>(x + BIAS);
    }

    int cellOf(float value, float size)
    {
        const float cell = std::floor(value / size);

        return static_cast<int>(std::clamp(cell, static_cast<float>(-LIMIT), static_cast<float>(LIMIT)));
    }
}

void kuge::SpatialGrid::clear(void)
{
    m_entries.clear();
    m_large.clear();
}

void kuge::SpatialGrid::cellsOf(const Aabb& box, int& x0, int& y0, int& x1, int& y1) const
{
    x0 = cellOf(box.min.x, m_cellSize);
    y0 = cellOf(box.min.y, m_cellSize);
    x1 = cellOf(box.max.x, m_cellSize);
    y1 = cellOf(box.max.y, m_cellSize);
}

void kuge::SpatialGrid::insert(std::uint32_t item, const Aabb& box)
{
    int x0, y0, x1, y1;

    cellsOf(box, x0, y0, x1, y1);
    const std::int64_t count = static_cast<std::int64_t>(x1 - x0 + 1) * (y1 - y0 + 1);

    if (count > MAX_CELLS_PER_ITEM) {
        m_large.push_back(item);
        return;
    }
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            m_entries.push_back({keyOf(x, y), item});
        }
    }
}

void kuge::SpatialGrid::finish(void)
{
    std::sort(m_entries.begin(), m_entries.end());
    std::sort(m_large.begin(), m_large.end());
}

void kuge::SpatialGrid::query(const Aabb& box, std::vector<std::uint32_t>& out) const
{
    int x0, y0, x1, y1;

    out.assign(m_large.begin(), m_large.end());
    cellsOf(box, x0, y0, x1, y1);
    const std::int64_t count = static_cast<std::int64_t>(x1 - x0 + 1) * (y1 - y0 + 1);

    if (count > 4096) {
        // A huge box: cheaper to look at everything than to visit each cell
        for (const Entry& entry : m_entries) {
            out.push_back(entry.item);
        }
    } else {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const std::uint64_t key = keyOf(x, y);
                auto it = std::lower_bound(m_entries.begin(), m_entries.end(), Entry{key, 0});

                for (; it != m_entries.end() && it->cell == key; ++it) {
                    out.push_back(it->item);
                }
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}
