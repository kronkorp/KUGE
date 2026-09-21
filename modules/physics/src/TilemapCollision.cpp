#include "TilemapCollision.hpp"
#include <format>

kuge::SolidGrid kuge::makeSolidGrid(const TileMap& map, const TileLayer& layer, Vec2 origin)
{
    SolidGrid grid;

    grid.reset(map.width(), map.height(), map.tileSize(), origin);
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            grid.set(x, y, map.isSolid(map.tileAt(layer, x, y)));
        }
    }
    return grid;
}

kuge::SolidGrid kuge::makeSolidGrid(const TileMap& map, std::string_view layer, Vec2 origin)
{
    const TileLayer* found = map.findLayer(layer);

    if (!found) {
        throw TileMapError(std::format("the tilemap has no layer '{}'", layer));
    }
    return makeSolidGrid(map, *found, origin);
}
