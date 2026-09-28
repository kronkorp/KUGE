#pragma once

#include "SolidGrid.hpp"
#include "TileMap.hpp"
#include <string_view>

namespace kuge
{

    //! Which tiles of a layer stop bodies: those the map says are solid
    //!
    //!     auto map = kuge::TileMap::load("level1.tilemap");
    //!     world.getResource<kuge::Physics2D>().tiles = kuge::makeSolidGrid(map, "ground");
    //!
    //! @param origin  Where the corner of the first tile is in the world
    //! @throw TileMapError if the map has no such layer
    SolidGrid makeSolidGrid(const TileMap& map, std::string_view layer, Vec2 origin = {});

    //! The same, for a layer that is at hand
    SolidGrid makeSolidGrid(const TileMap& map, const TileLayer& layer, Vec2 origin = {});

}
