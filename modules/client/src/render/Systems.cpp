#include "render/Systems.hpp"
#include "Ref.hpp"
#include "Time.hpp"
#include "backend/IRenderer2D.hpp"
#include "input/ActionState.hpp"
#include "input/InputMap.hpp"
#include "render/Components.hpp"
#include "render/Tilemap.hpp"
#include <algorithm>
#include <cmath>

bool kuge::SampleInput::handle(kw::World& world)
{
    world.getResource<ActionState>() =
        world.getResource<Ref<InputMap>>()->sampleTick(world.getResource<Time>().tick);
    return true;
}

bool kuge::SnapshotTransforms::handle(kw::World& world)
{
    auto view = world.view<Transform2D, PreviousTransform2D>();

    for (kw::Entity entity : view) {
        world.get<PreviousTransform2D>(entity).value = world.get<Transform2D>(entity);
    }
    return true;
}

bool kuge::SpriteRender::handle(kw::World& world)
{
    IRenderer2D& renderer = *world.getResource<Ref<IRenderer2D>>();
    const Camera2D& camera = world.getResource<Camera2D>();
    const auto& white = world.getResource<WhitePixel>().texture;
    const float alpha = static_cast<float>(world.getResource<Time>().alpha);
    const Vec2 screen = renderer.outputSize();
    const Rect visibleArea{0.0f, 0.0f, screen.x, screen.y};

    m_items.clear();
    auto view = world.view<Transform2D, Sprite>();
    for (kw::Entity entity : view) {
        const Sprite& sprite = world.get<Sprite>(entity);
        Transform2D transform = world.get<Transform2D>(entity);

        if (!sprite.visible) {
            continue;
        }
        if (world.has<PreviousTransform2D>(entity)) {
            transform = lerp(world.get<PreviousTransform2D>(entity).value, transform, alpha);
        }
        // A sheet gives both the picture and the part of it
        const std::shared_ptr<Texture>& texture = sprite.sheet ? sprite.sheet->texture : sprite.texture;
        const Rect source = sprite.sheet ? sprite.sheet->frame(sprite.frame) : sprite.source;
        Vec2 size = sprite.size;
        if (size.x == 0.0f && size.y == 0.0f) {
            size = source.w > 0.0f ? Vec2{source.w, source.h} : (texture ? texture->size() : Vec2{});
        }
        size = Vec2{size.x * transform.scale.x, size.y * transform.scale.y} * camera.zoom;
        const Vec2 at = camera.worldToScreen(transform.position, screen);
        const Rect destination{at.x - sprite.pivot.x * size.x, at.y - sprite.pivot.y * size.y, size.x, size.y};

        // Turned around its pivot, it can reach as far as its diagonal
        const float reach = transform.rotation == 0.0f ? 0.0f : std::hypot(size.x, size.y);
        const Rect bounds{destination.x - reach, destination.y - reach, destination.w + 2 * reach, destination.h + 2 * reach};

        if (!bounds.intersects(visibleArea)) {
            continue;
        }
        Item item;
        item.tiles = false;
        item.layer = sprite.layer;
        item.z = sprite.z;
        item.entity = entity;
        item.plain = texture == nullptr;
        item.texture = item.plain ? white->id() : texture->id();
        item.draw.texture = item.texture;
        item.draw.source = source;
        item.draw.destination = destination;
        item.draw.rotation = transform.rotation;
        item.draw.pivot = sprite.pivot;
        item.draw.tint = sprite.tint;
        item.draw.flipX = sprite.flipX;
        item.draw.flipY = sprite.flipY;
        m_items.push_back(item);
    }
    auto maps = world.view<Transform2D, TilemapView>();
    for (kw::Entity entity : maps) {
        const TilemapView& view = world.get<TilemapView>(entity);

        if (view.visible && view.map && view.tiles) {
            Item item{};

            item.layer = view.layer;
            item.z = view.z;
            item.texture = NO_TEXTURE;
            item.entity = entity;
            item.tiles = true;
            m_items.push_back(item);
        }
    }
    // A total order: the entity breaks the ties, so the result does not depend
    // on how the World happens to store them
    std::sort(m_items.begin(), m_items.end(), [](const Item& a, const Item& b) {
        if (a.layer != b.layer) {
            return a.layer < b.layer;
        }
        if (a.z != b.z) {
            return a.z < b.z;
        }
        if (a.texture != b.texture) {
            return a.texture < b.texture;
        }
        return a.entity < b.entity;
    });
    for (const Item& item : m_items) {
        if (item.tiles) {
            drawTiles(world, item.entity, renderer);
            continue;
        }
        const bool plainAndSimple = item.plain && item.draw.rotation == 0.0f && !item.draw.flipX && !item.draw.flipY;

        if (plainAndSimple) {
            renderer.fillRect(item.draw.destination, item.draw.tint);
        } else {
            renderer.drawTexture(item.draw);
        }
    }
    return true;
}

// The tiles of a tilemap layer that show on the screen. The edges of the tiles
// are rounded once per column and row, so that two tiles always meet: no seam
// shows between them, at any zoom.
void kuge::SpriteRender::drawTiles(kw::World& world, kw::Entity entity, IRenderer2D& renderer)
{
    const TilemapView& view = world.get<TilemapView>(entity);
    const Transform2D& transform = world.get<Transform2D>(entity);
    const Camera2D& camera = world.getResource<Camera2D>();
    const Vec2 screen = renderer.outputSize();
    const TileMap& map = *view.map;
    const TileRange range = visibleTiles(map, transform.position, transform.scale, camera, screen);

    if (range.empty() || !view.tiles->texture) {
        return;
    }
    const float width = map.tileSize() * std::fabs(transform.scale.x);
    const float height = map.tileSize() * std::fabs(transform.scale.y);

    m_columns.clear();
    m_rows.clear();
    for (int x = range.x0; x <= range.x1 + 1; ++x) {
        m_columns.push_back(std::round(camera.worldToScreen({transform.position.x + width * static_cast<float>(x), 0.0f}, screen).x));
    }
    for (int y = range.y0; y <= range.y1 + 1; ++y) {
        m_rows.push_back(std::round(camera.worldToScreen({0.0f, transform.position.y + height * static_cast<float>(y)}, screen).y));
    }
    auto drawLayer = [&](const TileLayer& layer) {
        TextureDraw draw;

        draw.texture = view.tiles->texture->id();
        draw.tint = view.tint;
        for (int y = range.y0; y <= range.y1; ++y) {
            for (int x = range.x0; x <= range.x1; ++x) {
                const TileId tile = map.tileAt(layer, x, y);

                if (tile == EMPTY_TILE) {
                    continue;
                }
                const std::size_t column = static_cast<std::size_t>(x - range.x0);
                const std::size_t row = static_cast<std::size_t>(y - range.y0);

                draw.source = view.tiles->frame(static_cast<int>(tile) - 1);
                draw.destination = {m_columns[column], m_rows[row], m_columns[column + 1] - m_columns[column],
                    m_rows[row + 1] - m_rows[row]};
                renderer.drawTexture(draw);
            }
        }
    };

    if (!view.mapLayer.empty()) {
        if (const TileLayer* layer = map.findLayer(view.mapLayer)) {
            drawLayer(*layer);
        }
        return;
    }
    for (const TileLayer& layer : map.layers()) {
        if (layer.visible) {
            drawLayer(layer);
        }
    }
}
