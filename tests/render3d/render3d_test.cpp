extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ClientModule.hpp"
#include "Components3D.hpp"
#include "Engine.hpp"
#include "Render3DModule.hpp"
#include "Scene3D.hpp"
#include "Transform3D.hpp"
#include "backend/dummy/DummyBackend.hpp"
#include "render/Components.hpp"
#include <functional>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Call = kuge::DummyRenderer::Call;

    struct Scene : kuge::Scene3D
    {
        using kuge::Scene::world;

        explicit Scene(std::function<void(Scene&)> setup) : m_setup(std::move(setup)) {}

        void onEnter(void) override
        {
            installClient3DSystems();
            if (m_setup) {
                m_setup(*this);
            }
        }

        std::function<void(Scene&)> m_setup;
    };

    // A windowed engine with the dummy backend and the 3D renderer, 64 x 48 pixels
    struct Fixture
    {
        kuge::DummyBackend dummy;
        kuge::Engine       engine;

        explicit Fixture(std::function<void(Scene&)> setup)
            : dummy(kuge::makeDummyBackend({64.0f, 48.0f})),
              engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed})
        {
            engine.addModule<kuge::ClientModule>(std::move(dummy.backend));
            engine.addModule<kuge::Render3DModule>();
            engine.scenes().change<Scene>(std::move(setup));
            engine.step(0.0);
        }

        const std::vector<Call>& frame(double seconds = 0.0)
        {
            engine.step(seconds);
            return dummy.renderer->lastFrame();
        }

        kw::World& world(void) { return static_cast<Scene*>(engine.scenes().top())->world(); }

        //! The pixel of the 3D image at (x, y) in the last frame
        const std::uint8_t* pixel(const std::vector<Call>& calls, int x, int y) const
        {
            for (const Call& call : calls) {
                if (call.kind == Call::Kind::Texture && call.destination.w == 64.0f) {
                    return dummy.renderer->texture(call.texture.texture)->rgba.data() + (y * 64 + x) * 4;
                }
            }
            return nullptr;
        }
    };

    // A 2 x 2 quad in the plane z = 0, facing +z, of one color, which the light does not change
    std::shared_ptr<const k3::Model> quad(bool lit = false)
    {
        auto mesh = std::make_shared<k3::Mesh>();
        auto material = std::make_shared<k3::Material>();
        auto model = std::make_shared<k3::Model>();

        mesh->positions = {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
        mesh->indices = {0, 1, 2, 0, 2, 3};
        mesh->computeNormals();
        material->diffuse = {1.f, 0.f, 0.f, 1.f};
        material->unlit = !lit;
        model->primitives.push_back({mesh, material});
        return model;
    }

    // The camera sees the quad at the center of the screen, from 5 meters
    void lookAtQuad(Scene& scene)
    {
        auto& camera = scene.world().getResource<k3::Camera>();

        camera.position = {0.f, 0.f, 5.f};
        camera.lookAt({0.f, 0.f, 0.f});
        scene.world().getResource<k3::Environment>().clearColor = {0.f, 0.f, 0.f, 1.f};
        scene.world().getResource<k3::Environment>().ambient = {0.f, 0.f, 0.f, 1.f};
    }

    kw::Entity spawnQuad(Scene& scene, float x = 0.0f, bool lit = false)
    {
        kw::World& world = scene.world();
        const kw::Entity entity = world.create();
        kuge::Transform3D transform;

        transform.translation = {x, 0.f, 0.f};
        world.add<kuge::Transform3D>(entity, transform);
        world.add<kuge::Model3D>(entity, kuge::Model3D{quad(lit)});
        return entity;
    }

    bool red(const std::uint8_t* pixel) { return pixel && pixel[0] > 200 && pixel[1] < 40 && pixel[2] < 40; }
}

Test(render3d, draws_a_model)
{
    Fixture fx([](Scene& scene) { lookAtQuad(scene); spawnQuad(scene); });
    const auto& calls = fx.frame();

    Assert(!calls.empty() && calls[0].kind == Call::Kind::Texture, "the 3D image is drawn");
    Assert(calls[0].destination.w == 64.0f && calls[0].destination.h == 48.0f, "on the whole screen");
    Assert(red(fx.pixel(calls, 32, 24)), "the quad at the center");
    Assert(!red(fx.pixel(calls, 1, 1)), "the corner is the clear color");
}

Test(render3d, follows_the_transform)
{
    Fixture fx([](Scene& scene) { lookAtQuad(scene); spawnQuad(scene, 100.0f); });
    const auto& calls = fx.frame();

    Assert(!red(fx.pixel(calls, 32, 24)), "far on the side, out of view");
}

Test(render3d, hidden_is_not_drawn)
{
    Fixture fx([](Scene& scene) { lookAtQuad(scene); spawnQuad(scene); });

    fx.world().get<kuge::Model3D>(*fx.world().view<kuge::Model3D>().begin()).visible = false;
    Assert(!red(fx.pixel(fx.frame(), 32, 24)), "not visible");
}

Test(render3d, sprites_are_over_the_3d)
{
    Fixture fx([](Scene& scene) {
        lookAtQuad(scene);
        spawnQuad(scene);

        kuge::Sprite sprite;
        const kw::Entity entity = scene.world().create();

        sprite.size = {10.0f, 10.0f};
        scene.world().add<kuge::Transform2D>(entity, kuge::Transform2D{});
        scene.world().add<kuge::Sprite>(entity, sprite);
    });
    const auto& calls = fx.frame();

    AssertEq(calls.size(), 2u, "the 3D image and the sprite");
    Assert(calls[0].kind == Call::Kind::Texture && calls[0].destination.w == 64.0f, "the 3D first");
    Assert(calls[1].kind == Call::Kind::Fill, "the sprite after");
}

Test(render3d, frame_texture_is_kept)
{
    Fixture fx([](Scene& scene) { lookAtQuad(scene); spawnQuad(scene); });
    fx.frame();
    const std::size_t textures = fx.dummy.renderer->textureCount();

    fx.frame();
    fx.frame();
    AssertEq(fx.dummy.renderer->textureCount(), textures, "updated in place, not made again");
}

Test(render3d, screen_resizes_the_image)
{
    Fixture fx([](Scene& scene) { lookAtQuad(scene); spawnQuad(scene); });

    fx.dummy.renderer->resize({32.0f, 24.0f});
    const auto& calls = fx.frame();

    Assert(calls[0].kind == Call::Kind::Texture, "drawn");
    AssertEq(fx.dummy.renderer->texture(calls[0].texture.texture)->width, 32, "at the new size");
}

Test(render3d, light_of_an_entity)
{
    Fixture dark([](Scene& scene) { lookAtQuad(scene); spawnQuad(scene, 0.0f, true); });
    Fixture lit([](Scene& scene) {
        lookAtQuad(scene);
        spawnQuad(scene, 0.0f, true);

        const kw::Entity sun = scene.world().create();

        scene.world().add<kuge::Light3D>(sun, kuge::Light3D{k3::Light::directional({0.f, 0.f, -1.f})});
    });

    Assert(!red(dark.pixel(dark.frame(), 32, 24)), "no light, no ambient: black");
    Assert(red(lit.pixel(lit.frame(), 32, 24)), "lit by the entity's light");
    AssertEq(lit.world().getResource<k3::Environment>().lights.size(), 0u, "the scene's lights are left as they were");
}

Test(render3d, previous_follows_the_tick)
{
    Fixture fx([](Scene& scene) {
        const kw::Entity entity = spawnQuad(scene, 1.0f);

        scene.world().add<kuge::PreviousTransform3D>(entity, kuge::PreviousTransform3D{});
    });

    fx.frame(1.0 / 60.0);
    for (kw::Entity entity : fx.world().view<kuge::PreviousTransform3D>()) {
        AssertEq(fx.world().get<kuge::PreviousTransform3D>(entity).value.translation.x, 1.0f, "where it was at the tick");
    }
}

Test(render3d, what_is_held_is_in_front)
{
    // A wall of blue right in front of the camera, and a small red thing held low on the right: the wall
    // does not hide it (it is drawn in a layer of its own, from the camera)
    const auto flat = [](float half, float r, float g, float b) {
        auto mesh = std::make_shared<k3::Mesh>();
        auto material = std::make_shared<k3::Material>();
        auto model = std::make_shared<k3::Model>();

        mesh->positions = {{-half, -half, 0.f}, {half, -half, 0.f}, {half, half, 0.f}, {-half, half, 0.f}};
        mesh->indices = {0, 1, 2, 0, 2, 3};
        mesh->computeNormals();
        material->diffuse = {r, g, b, 1.f};
        material->unlit = true;
        material->doubleSided = true;
        model->primitives.push_back({mesh, material});
        return std::shared_ptr<const k3::Model>(model);
    };
    Fixture fx([&flat](Scene& scene) {
        lookAtQuad(scene);

        const kw::Entity wall = scene.world().create();
        const kw::Entity held = scene.world().create();
        kuge::Transform3D transform;

        transform.translation = {0.f, 0.f, 4.0f};     // between the camera (z = 5) and the origin: 1 m ahead
        scene.world().add<kuge::Transform3D>(wall, transform);
        scene.world().add<kuge::Model3D>(wall, kuge::Model3D{flat(20.0f, 0.f, 0.f, 1.f)});
        scene.world().add<kuge::ViewModel3D>(held, kuge::ViewModel3D{flat(0.1f, 1.f, 0.f, 0.f)});
    });
    const auto& calls = fx.frame();
    const std::uint8_t* middle = fx.pixel(calls, 32, 24);
    const std::uint8_t* hand = fx.pixel(calls, 53, 41);

    Assert(middle && middle[2] > 200 && middle[0] < 40, "the wall is there");
    Assert(hand && red(hand), "and what is held is over it, low on the right");
}

Test(render3d, many_entities_one_model)
{
    // Three entities that share a model (so a mesh and a material) are drawn where each is, even though they are drawn
    // together; a fourth, of another model, goes with them
    Fixture fx([](Scene& scene) {
        lookAtQuad(scene);
        const auto shared = quad();

        for (const float x : {-2.0f, 0.0f, 2.0f}) {
            const kw::Entity entity = scene.world().create();
            kuge::Transform3D transform;

            transform.translation = {x, 0.0f, 0.0f};
            scene.world().add<kuge::Transform3D>(entity, transform);
            scene.world().add<kuge::Model3D>(entity, kuge::Model3D{shared});
        }
    });
    const auto& calls = fx.frame();

    Assert(red(fx.pixel(calls, 15, 24)) && red(fx.pixel(calls, 32, 24)) && red(fx.pixel(calls, 49, 24)), "one where each entity is");
    Assert(!red(fx.pixel(calls, 2, 24)) && !red(fx.pixel(calls, 61, 24)), "and nothing beyond them");
}
