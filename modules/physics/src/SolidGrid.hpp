#pragma once

#include "Shapes.hpp"
#include <cstdint>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A level made of tiles, some of which are solid
     *
     * What a tilemap gives to the physics: bodies are stopped by the solid
     * tiles like by walls. The tile (0, 0) has its top-left corner at origin.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct SolidGrid
    {
        int                        width    = 0;   //!< In tiles
        int                        height   = 0;
        float                      tileSize = 16.0f;
        Vec2                       origin{};
        std::vector<std::uint8_t>  solid;          //!< width * height, row after row, 0 or 1

        //! An empty level of width x height tiles
        void reset(int newWidth, int newHeight, float newTileSize, Vec2 newOrigin = {});

        bool isSolid(int x, int y) const noexcept
        {
            return x >= 0 && y >= 0 && x < width && y < height && solid[static_cast<std::size_t>(y) * width + x] != 0;
        }

        //! Out of the level: ignored
        void set(int x, int y, bool value) noexcept
        {
            if (x >= 0 && y >= 0 && x < width && y < height) {
                solid[static_cast<std::size_t>(y) * width + x] = value ? 1 : 0;
            }
        }

        Aabb tile(int x, int y) const noexcept
        {
            const Vec2 corner{origin.x + tileSize * static_cast<float>(x), origin.y + tileSize * static_cast<float>(y)};

            return {corner, {corner.x + tileSize, corner.y + tileSize}};
        }

        //! The tile a point is in (it may be out of the level)
        void tileAt(Vec2 point, int& x, int& y) const noexcept;

        bool isSolidAt(Vec2 point) const noexcept
        {
            int x, y;

            tileAt(point, x, y);
            return isSolid(x, y);
        }

        bool empty(void) const noexcept { return width == 0 || height == 0; }
    };

}
