#include "render/Tilemap.hpp"
#include <algorithm>
#include <cmath>

kuge::TileRange kuge::visibleTiles(const TileMap& map, Vec2 position, Vec2 scale, const Camera2D& camera, Vec2 screen)
{
    const float width = map.tileSize() * std::fabs(scale.x);
    const float height = map.tileSize() * std::fabs(scale.y);
    TileRange range;

    if (!(width > 0.0f) || !(height > 0.0f) || !(camera.zoom > 0.0f)) {
        return range;
    }
    const CameraView shown = camera.view(screen);
    const Vec2 topLeft = shown.screenToWorld({shown.area.x, shown.area.y});
    const Vec2 bottomRight = shown.screenToWorld({shown.area.x + shown.area.w, shown.area.y + shown.area.h});

    range.x0 = std::max(0, static_cast<int>(std::floor((topLeft.x - position.x) / width)));
    range.y0 = std::max(0, static_cast<int>(std::floor((topLeft.y - position.y) / height)));
    range.x1 = std::min(map.width() - 1, static_cast<int>(std::ceil((bottomRight.x - position.x) / width)) - 1);
    range.y1 = std::min(map.height() - 1, static_cast<int>(std::ceil((bottomRight.y - position.y) / height)) - 1);
    return range;
}
