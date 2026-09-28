#include "SolidGrid.hpp"
#include <cmath>

void kuge::SolidGrid::reset(int newWidth, int newHeight, float newTileSize, Vec2 newOrigin)
{
    width = newWidth < 0 ? 0 : newWidth;
    height = newHeight < 0 ? 0 : newHeight;
    tileSize = newTileSize;
    origin = newOrigin;
    solid.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
}

void kuge::SolidGrid::tileAt(Vec2 point, int& x, int& y) const noexcept
{
    x = static_cast<int>(std::floor((point.x - origin.x) / tileSize));
    y = static_cast<int>(std::floor((point.y - origin.y) / tileSize));
}
