#pragma once

#include "TileMap.hpp"
#include "render/Components.hpp"
#include "render/Spritesheet.hpp"
#include <memory>
#include <string>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Draws a layer of a TileMap (with a Transform2D, whose position is
     *         where the corner of tile (0, 0) is, and whose scale stretches the tiles)
     *
     * Only the tiles that show on the screen are drawn. It is sorted with the
     * sprites by layer, so a sprite can go between two layers of tiles: use
     * one entity per layer of the map.
     *
     *     auto map = std::make_shared<kuge::TileMap>(kuge::TileMap::load("level1.tilemap"));
     *     world.add<kuge::Transform2D>(e, kuge::Transform2D{});
     *     world.add<kuge::TilemapView>(e, kuge::TilemapView{map, tiles, "ground", -10});
     *
     * A tilemap is not turned: the rotation of the Transform2D is ignored.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct TilemapView
    {
        std::shared_ptr<const TileMap>     map;
        std::shared_ptr<const Spritesheet> tiles;      //!< Tile 1 is its first cell
        std::string                        mapLayer;   //!< Which layer of the map. Empty: all the visible ones, in order.
        int                                layer = 0;  //!< Draw order among sprites (see Sprite::layer)
        float                              z = 0.0f;
        Color                              tint{};
        bool                               visible = true;
    };

    //! A rectangle of tiles, both ends included
    struct TileRange
    {
        int x0 = 0;
        int y0 = 0;
        int x1 = -1;
        int y1 = -1;

        bool empty(void) const noexcept { return x1 < x0 || y1 < y0; }
    };

    //! The tiles of a map that show on a screen, clamped to the map
    //! @param position  Where the corner of tile (0, 0) is, in the world
    TileRange visibleTiles(const TileMap& map, Vec2 position, Vec2 scale, const Camera2D& camera, Vec2 screen);

}
