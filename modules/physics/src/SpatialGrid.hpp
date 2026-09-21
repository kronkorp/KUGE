#pragma once

#include "Shapes.hpp"
#include <cstdint>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Finds what is near a box without looking at everything (the
     *         "broad phase" of a physics engine)
     *
     * The world is cut in square cells. Each item is written down in the cells
     * its box covers; asking for a box gives what is in the cells it covers.
     * Items very large (the floor of a level) are kept apart and always given.
     *
     * The answers are always sorted and without duplicates, so what is done
     * with them does not depend on how the grid is stored.
     */
    ////////////////////////////////////////////////////////////////////////////
    class SpatialGrid
    {
        public:
            explicit SpatialGrid(float cellSize = 64.0f) : m_cellSize(cellSize) {}

            void  setCellSize(float cellSize) noexcept { m_cellSize = cellSize; }
            float cellSize(void) const noexcept { return m_cellSize; }

            void clear(void);

            //! An item is a number chosen by the caller. It can be added several times.
            void insert(std::uint32_t item, const Aabb& box);

            //! To call once all is inserted, before the first query
            void finish(void);

            //! What may touch box: everything whose cells are shared with it. It
            //! can be more than what touches it, never less. Sorted, once each.
            void query(const Aabb& box, std::vector<std::uint32_t>& out) const;

        private:
            struct Entry
            {
                std::uint64_t cell;
                std::uint32_t item;

                bool operator<(const Entry& o) const { return cell != o.cell ? cell < o.cell : item < o.item; }
            };

            static constexpr int MAX_CELLS_PER_ITEM = 64;

            void cellsOf(const Aabb& box, int& x0, int& y0, int& x1, int& y1) const;

            float                      m_cellSize;
            std::vector<Entry>         m_entries;
            std::vector<std::uint32_t> m_large;
    };

}
