#include "Systems3D.hpp"
#include "Components3D.hpp"
#include "Ref.hpp"
#include "Time.hpp"
#include "Transform3D.hpp"
#include <kronk3d/render/IRasterizer.hpp>

bool kuge::SnapshotTransforms3D::handle(kw::World& world)
{
    auto view = world.view<Transform3D, PreviousTransform3D>();

    for (kw::Entity entity : view) {
        world.get<PreviousTransform3D>(entity).value = world.get<Transform3D>(entity);
    }
    return true;
}

namespace
{

    //! Where an entity is on this frame: between its previous and current tick, if it keeps the previous one
    kuge::Transform3D shown(kw::World& world, kw::Entity entity, float alpha)
    {
        const kuge::Transform3D& transform = world.get<kuge::Transform3D>(entity);

        if (world.has<kuge::PreviousTransform3D>(entity)) {
            return kuge::lerp(world.get<kuge::PreviousTransform3D>(entity).value, transform, alpha);
        }
        return transform;
    }

}

bool kuge::ModelRender::handle(kw::World& world)
{
    k3::IRasterizer& rasterizer = *world.getResource<Ref<k3::IRasterizer>>();
    IRenderer2D& renderer = *world.getResource<Ref<IRenderer2D>>();
    const Vec2 screen = renderer.outputSize();
    const auto width = static_cast<std::uint32_t>(screen.x);
    const auto height = static_cast<std::uint32_t>(screen.y);

    if (width == 0 || height == 0) {
        return true;
    }
    if (rasterizer.width() != width || rasterizer.height() != height) {
        rasterizer.resize(width, height);
    }
    const float alpha = static_cast<float>(world.getResource<Time>().alpha);
    k3::Environment& environment = world.getResource<k3::Environment>();
    const std::size_t sceneLights = environment.lights.size();

    // The environment is only read by endFrame(), below: the lights of the entities are added to the
    // scene's own for this frame, then taken off
    for (kw::Entity entity : world.view<Light3D>()) {
        if (environment.lights.size() >= k3::MAX_LIGHTS) {
            break;
        }
        k3::Light light = world.get<Light3D>(entity).light;

        if (world.has<Transform3D>(entity)) {
            const k3::Math::Matrix4 matrix = shown(world, entity, alpha).matrix();

            light.position = matrix.transformPoint(light.position);
            light.direction = matrix.transformDirection(light.direction);
        }
        environment.lights.push_back(light);
    }
    rasterizer.beginFrame(world.getResource<k3::Camera>(), environment);
    for (kw::Entity entity : world.view<Transform3D, Model3D>()) {
        const Model3D& model = world.get<Model3D>(entity);

        if (model.visible && model.model) {
            rasterizer.draw(*model.model, shown(world, entity, alpha).matrix());
        }
    }
    rasterizer.endFrame();
    environment.lights.resize(sceneLights);

    const k3::Image image = rasterizer.readPixels();

    if (image.empty()) {
        return true;
    }
    if (!m_frame || m_frame->width() != static_cast<int>(image.width) || m_frame->height() != static_cast<int>(image.height)) {
        m_frame = Texture::fromPixels(renderer, static_cast<int>(image.width), static_cast<int>(image.height), image.pixels);
    } else {
        m_frame->update(image.pixels);
    }
    TextureDraw draw;

    draw.texture = m_frame->id();
    draw.destination = Rect{0.0f, 0.0f, screen.x, screen.y};
    renderer.drawTexture(draw);
    return true;
}
