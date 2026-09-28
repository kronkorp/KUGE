#pragma once

#include "Math2D.hpp"
#include "render/Texture.hpp"
#include <algorithm>
#include <memory>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A picture cut in equal cells: the frames of an animation, or the
     *         tiles of a tileset
     *
     * Cells are numbered row after row from the top left, starting at 0. (A
     * tilemap counts its tiles from 1, since 0 means "nothing".)
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Spritesheet
    {
        std::shared_ptr<Texture> texture;
        int frameWidth  = 16;
        int frameHeight = 16;
        int margin      = 0;   //!< Around the whole picture
        int spacing     = 0;   //!< Between two cells
        int columns     = 0;   //!< 0: as many as fit in the picture

        //! How many cells there are in a row
        int columnsOf(void) const noexcept
        {
            if (columns > 0) {
                return columns;
            }
            if (!texture || frameWidth <= 0) {
                return 0;
            }
            return std::max(0, (texture->width() - 2 * margin + spacing) / (frameWidth + spacing));
        }

        int rowsOf(void) const noexcept
        {
            if (!texture || frameHeight <= 0) {
                return 0;
            }
            return std::max(0, (texture->height() - 2 * margin + spacing) / (frameHeight + spacing));
        }

        int frameCount(void) const noexcept { return columnsOf() * rowsOf(); }

        //! The part of the picture for a cell. Out of range: the first one.
        Rect frame(int index) const noexcept
        {
            const int perRow = columnsOf();

            if (perRow <= 0 || index < 0 || index >= frameCount()) {
                index = 0;
            }
            const int column = perRow > 0 ? index % perRow : 0;
            const int row = perRow > 0 ? index / perRow : 0;

            return {static_cast<float>(margin + column * (frameWidth + spacing)),
                    static_cast<float>(margin + row * (frameHeight + spacing)),
                    static_cast<float>(frameWidth), static_cast<float>(frameHeight)};
        }
    };

}
