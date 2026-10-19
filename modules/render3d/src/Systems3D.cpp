#include "Systems3D.hpp"
#include "AnimationState3D.hpp"
#include "Animator3D.hpp"
#include "Components3D.hpp"
#include "animation/Animation.hpp"
#include "input/InputMap.hpp"
#include "Ref.hpp"
#include "Time.hpp"
#include "Transform3D.hpp"
#include <algorithm>
#include <map>
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

    //! The cues of the clip that plays, crossed between the time `before` (-1: it just began, so a cue at 0 counts) and `after`
    void fireCues(kw::Entity entity, const kuge::Animator3D& animation, float before, float after,
        std::vector<kuge::AnimationEvent>& events)
    {
        if (!animation.cues) {
            return;
        }
        const auto found = animation.cues->find(animation.animator.clip());

        if (found == animation.cues->end()) {
            return;
        }
        const bool wrapped = animation.options.loop && after < before;   // a loop began again

        for (const kuge::Cue3D& cue : found->second) {
            const bool crossed = wrapped ? (cue.time > before || cue.time <= after) : (cue.time > before && cue.time <= after);

            if (crossed) {
                events.push_back({entity, cue.name});
            }
        }
    }

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

bool kuge::PlayReplicatedClips::handle(kw::World& world)
{
    for (kw::Entity entity : world.view<Animator3D, AnimationState3D>()) {
        Animator3D& animation = world.get<Animator3D>(entity);
        const AnimationState3D& state = world.get<AnimationState3D>(entity);

        if (state.clip.empty() || !animation.model
            || (animation.stateSerial == state.serial && animation.animator.clip() == state.clip)) {
            continue;
        }
        if (animation.stateSerial != state.serial) {
            animation.animator.stop();   // the same clip again starts over
        }
        animation.play(state.clip, k3::PlayOptions{.loop = state.loop});
        animation.stateSerial = state.serial;
    }
    return true;
}

bool kuge::AnimateModels::handle(kw::World& world)
{
    auto& events = world.getResource<AnimationEvents>().list;
    const float dt = static_cast<float>(world.getResource<Time>().dt);
    const std::uint64_t revision = world.getResource<Ref<const ModelRevision>>()->animated;

    m_entities.clear();
    for (kw::Entity entity : world.view<Animator3D>()) {
        m_entities.push_back(entity);
    }
    std::sort(m_entities.begin(), m_entities.end());
    for (kw::Entity entity : m_entities) {
        Animator3D& animation = world.get<Animator3D>(entity);

        if (!animation.model) {
            continue;
        }
        if (animation.revision && *animation.revision != revision) {
            // The model was read again: what the Animator keeps of the old one is no longer right
            const std::string clip(animation.animator.clip());
            const std::string next = animation.next;
            const k3::PlayOptions options = animation.options;

            animation.animator = k3::Animator(animation.model);
            animation.seen = -1;
            if (!clip.empty()) {
                animation.play(clip, options, next);
            }
        }
        animation.revision = revision;
        if (animation.animator.clip().empty()) {
            continue;
        }
        const float before = animation.seen;

        animation.animator.update(dt);
        animation.seen = animation.animator.time();
        fireCues(entity, animation, before, animation.seen, events);
        if (animation.animator.finished() && !animation.next.empty()) {
            const std::string next = std::move(animation.next);

            animation.next.clear();
            animation.play(next);
        }
    }
    return true;
}

bool kuge::UpdateCamera::handle(kw::World& world)
{
    kw::Entity chosen = 0;
    bool found = false;

    for (kw::Entity entity : world.view<CameraRig, Transform3D>()) {
        if (!found || entity < chosen) {
            chosen = entity;
            found = true;
        }
    }
    if (!found) {
        return true;
    }
    CameraRig& rig = world.get<CameraRig>(chosen);

    if (rig.look) {
        const Vec2 raw = world.getResource<Ref<InputMap>>()->mouseDelta();
        const Vec2 mouse{std::clamp(raw.x, -rig.maxMove, rig.maxMove), std::clamp(raw.y, -rig.maxMove, rig.maxMove)};

        rig.yaw += mouse.x * rig.sensitivity;
        rig.pitch = std::clamp(rig.pitch - mouse.y * rig.sensitivity, rig.minPitch, rig.maxPitch);
    }
    const k3::Math::Vector3f at = shown(world, chosen, static_cast<float>(world.getResource<Time>().alpha)).translation;
    k3::Camera& camera = world.getResource<k3::Camera>();

    camera.yaw = rig.yaw;
    camera.pitch = rig.pitch;
    const k3::Math::Vector3f point = at + rig.offset;

    if (rig.mode == CameraRig::Mode::Orbit) {
        float distance = rig.distance;

        if (rig.obstacle) {
            const float hit = rig.obstacle(point, camera.forward() * -1.0f, rig.distance);

            if (hit >= 0.0f && hit < rig.distance) {
                distance = std::max(hit - rig.margin, 0.05f);
            }
        }
        camera.position = point - camera.forward() * distance;
    } else {
        camera.position = point;
    }
    return true;
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
    IOpenGL* gl = world.getResource<GlContext>().gl;

    if (gl) {
        gl->makeCurrent();
    }
    if (rasterizer.width() != width || rasterizer.height() != height) {
        if (gl) {
            gl->resize(static_cast<int>(width), static_cast<int>(height));
        }
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
    // The models, a mesh with its material at a time: what many entities share (the crates of a level, the trees of a forest)
    // is drawn once with all its places, which the GPU does in a single call
    struct Instances
    {
        std::shared_ptr<const k3::Mesh>     mesh;
        std::shared_ptr<const k3::Material> material;
        std::vector<k3::Math::Matrix4>      places;
    };
    std::vector<Instances> groups;
    std::map<std::pair<const k3::Mesh*, const k3::Material*>, std::size_t> found;

    for (kw::Entity entity : world.view<Transform3D, Model3D>()) {
        const Model3D& model = world.get<Model3D>(entity);

        if (!model.visible || !model.model) {
            continue;
        }
        const k3::Math::Matrix4 place = shown(world, entity, alpha).matrix();

        for (const k3::Primitive& primitive : model.model->primitives) {
            const auto key = std::make_pair(primitive.mesh.get(), primitive.material.get());
            const auto [at, isNew] = found.try_emplace(key, groups.size());

            if (isNew) {
                groups.push_back({primitive.mesh, primitive.material, {}});
            }
            groups[at->second].places.push_back(place);
        }
    }
    for (const Instances& group : groups) {
        if (group.places.size() == 1) {
            rasterizer.draw(group.mesh, group.material, group.places.front());
        } else {
            rasterizer.drawInstances(group.mesh, group.material, group.places);
        }
    }
    for (kw::Entity entity : world.view<Transform3D, Animator3D>()) {
        Animator3D& animation = world.get<Animator3D>(entity);

        if (animation.visible && animation.model) {
            animation.animator.draw(rasterizer, shown(world, entity, alpha).matrix());
        }
    }
    // What the player holds goes in a layer of its own, over the scene
    for (kw::Entity entity : world.view<ViewModel3D>()) {
        const ViewModel3D& held = world.get<ViewModel3D>(entity);

        if (!held.visible || !held.model) {
            continue;
        }
        k3::Camera layer;

        layer.position = {0.0f, 0.0f, 0.0f};
        layer.fovY = held.fovY;
        layer.nearPlane = 0.02f;
        layer.farPlane = 10.0f;
        rasterizer.beginLayer(layer);
        rasterizer.draw(*held.model, k3::Math::Matrix4::translate({held.offset.x, held.offset.y, held.offset.z + held.kick}));
        break;       // (one is held at a time: the lowest entity)
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
