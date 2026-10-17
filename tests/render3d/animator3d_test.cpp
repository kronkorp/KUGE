extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Animator3D.hpp"
#include "Components3D.hpp"
#include "Transform3D.hpp"
#include "animation/Animation.hpp"
#include "fixture.hpp"
#include <string>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using render3d::Fixture;

    // One triangle in an embedded buffer
    const char* GLTF =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
        R"("bufferViews":[{"buffer":0,"byteLength":36}],)"
        R"("buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}]})";

    // A red quad on a root node. Clips: "walk" (1 s, slides it 10 meters), "idle" (0.5 s), "attack" (0.25 s)
    std::shared_ptr<const k3::AnimatedModel> character()
    {
        auto model = std::make_shared<k3::AnimatedModel>();
        auto mesh = std::make_shared<k3::Mesh>();
        auto material = std::make_shared<k3::Material>();
        k3::Animation walk;
        k3::AnimationChannel slide;

        mesh->positions = {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
        mesh->indices = {0, 1, 2, 0, 2, 3};
        mesh->computeNormals();
        material->diffuse = {1.f, 0.f, 0.f, 1.f};
        material->unlit = true;
        model->nodes.push_back(k3::Node{"root", -1, {}});
        model->primitives.push_back({mesh, material, 0, -1});
        slide.node = 0;
        slide.path = k3::AnimationChannel::Path::Translation;
        slide.times = {0.f, 1.f};
        slide.values = {{0.f, 0.f, 0.f, 0.f}, {10.f, 0.f, 0.f, 0.f}};
        walk.name = "walk";
        walk.duration = 1.f;
        walk.channels.push_back(slide);
        model->animations.push_back(walk);
        model->animations.push_back(k3::Animation{"idle", 0.5f, {}});
        model->animations.push_back(k3::Animation{"attack", 0.25f, {}});
        return model;
    }

    kw::Entity spawn(Fixture& fx, kuge::Animator3D animator)
    {
        kw::World& world = fx.world();
        const kw::Entity entity = world.create();

        world.add<kuge::Transform3D>(entity, kuge::Transform3D{});
        world.add<kuge::Animator3D>(entity, std::move(animator));
        return entity;
    }

    // A tick of the simulation (and one frame)
    void tick(Fixture& fx, int count = 1)
    {
        for (int i = 0; i < count; ++i) {
            fx.engine.step(1.0 / 60.0);
        }
    }

    void lookAtOrigin(Fixture& fx)
    {
        auto& camera = fx.world().getResource<k3::Camera>();

        camera.position = {0.f, 0.f, 5.f};
        camera.lookAt({0.f, 0.f, 0.f});
        fx.world().getResource<k3::Environment>().clearColor = {0.f, 0.f, 0.f, 1.f};
    }

    bool redAtCenter(Fixture& fx)
    {
        for (const auto& call : fx.dummy.renderer->lastFrame()) {
            if (call.kind == kuge::DummyRenderer::Call::Kind::Texture && call.destination.w == 64.0f) {
                const std::uint8_t* pixel = fx.dummy.renderer->texture(call.texture.texture)->rgba.data() + (24 * 64 + 32) * 4;

                return pixel[0] > 200 && pixel[1] < 40 && pixel[2] < 40;
            }
        }
        return false;
    }
}

Test(animator3d, cues_are_crossed_once)
{
    Fixture fx;
    auto cues = std::make_shared<kuge::ClipCues>();
    int starts = 0;
    int steps = 0;

    (*cues)["walk"] = {{0.0f, "start"}, {0.5f, "step"}};
    spawn(fx, kuge::Animator3D::of(character(), "walk", {}, cues));
    for (int i = 0; i < 100; ++i) {
        tick(fx);
        for (const auto& event : fx.world().getResource<kuge::AnimationEvents>().list) {
            starts += event.name == "start";
            steps += event.name == "step";
        }
    }
    // 100 ticks = 1.67 s of a clip of 1 s: it began, and began again once; it passed 0.5 and 1.5
    AssertEq(starts, 2, "start: at the beginning, and when the clip loops");
    AssertEq(steps, 2, "step: at 0.5 and 1.5");
}

Test(animator3d, the_next_clip_follows)
{
    Fixture fx;
    const kw::Entity entity = spawn(fx, kuge::Animator3D::of(character(), "idle"));
    auto& animator = fx.world().get<kuge::Animator3D>(entity);

    Assert(animator.play("attack", k3::PlayOptions{.loop = false}, "idle"), "the clip exists");
    Assert(!animator.play("fly"), "this one does not");
    tick(fx, 10);
    Assert(fx.world().get<kuge::Animator3D>(entity).animator.clip() == "attack", "0.17 s: still attacking");
    tick(fx, 10);
    Assert(fx.world().get<kuge::Animator3D>(entity).animator.clip() == "idle", "0.33 s: back to idle");
}

Test(animator3d, a_clip_moves_the_model)
{
    Fixture fx;

    lookAtOrigin(fx);
    spawn(fx, kuge::Animator3D::of(character(), "walk", k3::PlayOptions{.loop = false}));
    tick(fx);
    Assert(redAtCenter(fx), "at the start of the clip, in front of the camera");
    tick(fx, 30);
    Assert(!redAtCenter(fx), "half a second later, 5 meters away");
}

Test(animator3d, hidden_is_not_drawn)
{
    Fixture fx;
    const kw::Entity entity = spawn(fx, kuge::Animator3D::of(character()));

    lookAtOrigin(fx);
    tick(fx);
    Assert(redAtCenter(fx), "the rest pose");
    fx.world().get<kuge::Animator3D>(entity).visible = false;
    tick(fx);
    Assert(!redAtCenter(fx), "not visible");
}

Test(animator3d, a_reloaded_model_restarts)
{
    const render3d::Folder folder("anim_reload");
    Fixture fx;
    const auto file = folder.write("scene.gltf", GLTF);
    const auto loaded = fx.models->animatedModels().load(file);
    const kw::Entity entity = spawn(fx, kuge::Animator3D::of(character(), "walk"));

    tick(fx, 10);
    Assert(fx.world().get<kuge::Animator3D>(entity).animator.time() > 0.15f, "the clip has been playing");
    // A glTF of the manager is read again: what an Animator3D kept of a model may be wrong, so they start again
    folder.write("scene.gltf", std::string(GLTF) + " ", 1);
    AssertEq(fx.models->reloadAssets(), 1u, "the glTF is read again");
    tick(fx);
    Assert(fx.world().get<kuge::Animator3D>(entity).animator.clip() == "walk", "the same clip");
    Assert(fx.world().get<kuge::Animator3D>(entity).animator.time() < 0.05f, "from its beginning");
}
