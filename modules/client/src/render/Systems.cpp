#include "render/Systems.hpp"
#include "DebugDraw.hpp"
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
    const CameraView shown = camera.view(screen);
    const Rect& visibleArea = shown.area;

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
        size = Vec2{size.x * transform.scale.x * shown.scale.x, size.y * transform.scale.y * shown.scale.y};
        const Vec2 at = shown.worldToScreen(transform.position);
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
    // Bars and other cameras' parts of the window stay as they are: nothing is drawn outside the area
    const bool clipped = shown.area != Rect{0.0f, 0.0f, screen.x, screen.y};

    if (clipped) {
        renderer.setClip(shown.area);
    }
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
    if (clipped) {
        renderer.setClip(std::nullopt);
    }
    return true;
}

bool kuge::DebugRender::handle(kw::World& world)
{
    const DebugDraw& debug = world.getResource<DebugDraw>();

    if (!debug.colliders && !debug.bounds && !debug.camera) {
        return true;
    }
    IRenderer2D& renderer = *world.getResource<Ref<IRenderer2D>>();
    const Vec2 screen = renderer.outputSize();
    const CameraView view = world.getResource<Camera2D>().view(screen);
    const auto place = [&](Vec2 center, Vec2 size) {
        const Vec2 at = view.worldToScreen(center);
        const Vec2 pixels{size.x * view.scale.x, size.y * view.scale.y};

        return Rect{at.x - pixels.x * 0.5f, at.y - pixels.y * 0.5f, pixels.x, pixels.y};
    };

    renderer.setClip(view.area);
    if (debug.colliders) {
        for (const DebugShape& shape : debug.shapes) {
            const Color color = shape.kind == DebugKind::Static ? Color{80, 140, 255, 255}
                : shape.kind == DebugKind::Dynamic ? Color{90, 230, 110, 255}
                : shape.kind == DebugKind::Sensor ? Color{250, 220, 70, 255} : Color{255, 255, 255, 255};
            // A circle is shown by its box: the renderer draws rectangles
            const Vec2 size = shape.form == DebugShape::Form::Circle ? Vec2{shape.size.x * 2.0f, shape.size.x * 2.0f} : shape.size;

            renderer.strokeRect(place(shape.center, size), color);
        }
    }
    if (debug.bounds) {
        for (kw::Entity entity : world.view<Transform2D, Sprite>()) {
            const Sprite& sprite = world.get<Sprite>(entity);
            const Transform2D& transform = world.get<Transform2D>(entity);
            const std::shared_ptr<Texture>& texture = sprite.sheet ? sprite.sheet->texture : sprite.texture;
            const Rect source = sprite.sheet ? sprite.sheet->frame(sprite.frame) : sprite.source;
            Vec2 size = sprite.size;

            if (!sprite.visible) {
                continue;
            }
            if (size.x == 0.0f && size.y == 0.0f) {
                size = source.w > 0.0f ? Vec2{source.w, source.h} : (texture ? texture->size() : Vec2{});
            }
            size = {size.x * transform.scale.x, size.y * transform.scale.y};
            // The pivot is the point of the sprite at its position
            const Vec2 center{transform.position.x + (0.5f - sprite.pivot.x) * size.x, transform.position.y + (0.5f - sprite.pivot.y) * size.y};

            renderer.strokeRect(place(center, size), Color{255, 130, 220, 255});
        }
    }
    renderer.setClip(std::nullopt);
    if (debug.camera) {
        renderer.strokeRect(view.area, Color{255, 60, 60, 255});
        const Vec2 middle = view.center();

        renderer.fillRect({middle.x - 6.0f, middle.y - 1.0f, 12.0f, 2.0f}, Color{255, 60, 60, 255});
        renderer.fillRect({middle.x - 1.0f, middle.y - 6.0f, 2.0f, 12.0f}, Color{255, 60, 60, 255});
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
    const CameraView shown = camera.view(screen);
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
        m_columns.push_back(std::round(shown.worldToScreen({transform.position.x + width * static_cast<float>(x), 0.0f}).x));
    }
    for (int y = range.y0; y <= range.y1 + 1; ++y) {
        m_rows.push_back(std::round(shown.worldToScreen({0.0f, transform.position.y + height * static_cast<float>(y)}).y));
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
