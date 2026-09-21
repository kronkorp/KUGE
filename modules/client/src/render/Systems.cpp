#include "render/Systems.hpp"
#include "Ref.hpp"
#include "Time.hpp"
#include "backend/IRenderer2D.hpp"
#include "input/ActionState.hpp"
#include "input/InputMap.hpp"
#include "render/Components.hpp"
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
        Vec2 size = sprite.size;
        if (size.x == 0.0f && size.y == 0.0f) {
            size = sprite.source.w > 0.0f ? Vec2{sprite.source.w, sprite.source.h}
                : (sprite.texture ? sprite.texture->size() : Vec2{});
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
        item.layer = sprite.layer;
        item.z = sprite.z;
        item.entity = entity;
        item.plain = sprite.texture == nullptr;
        item.texture = item.plain ? white->id() : sprite.texture->id();
        item.draw.texture = item.texture;
        item.draw.source = sprite.source;
        item.draw.destination = destination;
        item.draw.rotation = transform.rotation;
        item.draw.pivot = sprite.pivot;
        item.draw.tint = sprite.tint;
        item.draw.flipX = sprite.flipX;
        item.draw.flipY = sprite.flipY;
        m_items.push_back(item);
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
        const bool plainAndSimple = item.plain && item.draw.rotation == 0.0f && !item.draw.flipX && !item.draw.flipY;

        if (plainAndSimple) {
            renderer.fillRect(item.draw.destination, item.draw.tint);
        } else {
            renderer.drawTexture(item.draw);
        }
    }
    return true;
}
