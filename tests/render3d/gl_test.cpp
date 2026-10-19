extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ClientModule.hpp"
#include "Components3D.hpp"
#include "Engine.hpp"
#include "Render3DModule.hpp"
#include "Scene3D.hpp"
#include "Transform3D.hpp"
#include "backend/SdlBackend.hpp"
#include <cstdlib>
#include <functional>

// NOTE: kronklab test names are limited to 31 characters.

// Render3DModule on the GPU. It needs a machine with OpenGL 3.3: SDL's "offscreen" video driver gives a context with no
// window shown. Where there is none (a CI machine, Windows), a test says so and passes.

namespace
{
    struct Scene : kuge::Scene3D
    {
        using kuge::Scene::world;

        explicit Scene(std::function<void(Scene&)> setup) : m_setup(std::move(setup)) {}

        void onEnter(void) override
        {
            installClient3DSystems();
            m_setup(*this);
        }

        std::function<void(Scene&)> m_setup;
    };

    // A real engine with the SDL backend (not shown) and the 3D renderer asked for in one way or the other
    struct Frame
    {
        std::unique_ptr<kuge::Engine> engine;
        kuge::ClientModule*           client = nullptr;
        kuge::Render3DModule*         module = nullptr;
        std::optional<kuge::Image>    image;
        bool                          available = false;     //!< false: this machine cannot do what was asked

        Frame(kuge::Rendering rendering, std::function<void(Scene&)> setup)
        {
            setenv("SDL_VIDEODRIVER", "offscreen", 1);
            setenv("SDL_RENDER_DRIVER", "software", 1);
            setenv("SDL_AUDIODRIVER", "dummy", 1);
            kuge::WindowConfig window;

            window.width = 96;
            window.height = 64;
            window.openGL = rendering != kuge::Rendering::Software;
            try {
                engine = std::make_unique<kuge::Engine>(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});
                client = &engine->addModule<kuge::ClientModule>(kuge::makeSdlBackend(window));
                module = &engine->addModule<kuge::Render3DModule>(*client, kuge::Render3DConfig{.rendering = rendering});
            } catch (const std::exception& error) {
                std::fprintf(stderr, "(no %s here: %s)\n", rendering == kuge::Rendering::OpenGL ? "OpenGL" : "video", error.what());
                return;
            }
            available = true;
            engine->scenes().change<Scene>(std::move(setup));
            engine->step(0.0);
            client->requestScreenshot();
            engine->step(0.0);
            image = client->takeScreenshot();
        }

        const std::uint8_t* pixel(int x, int y) const { return image->rgba.data() + (static_cast<std::size_t>(y) * image->width + x) * 4; }
    };

    // A cube with a light, a sky color and a tilted view: something with edges, depth and shading
    void cube(Scene& scene)
    {
        auto& camera = scene.world().getResource<k3::Camera>();
        auto& environment = scene.world().getResource<k3::Environment>();
        auto mesh = std::make_shared<k3::Mesh>();
        auto material = std::make_shared<k3::Material>();
        auto model = std::make_shared<k3::Model>();
        const k3::Math::Vector3f faces[6][3] = {
            {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
            {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
        };

        for (const auto& face : faces) {
            const auto first = static_cast<std::uint32_t>(mesh->positions.size());

            for (const auto& corner : {std::pair{-1.f, -1.f}, std::pair{1.f, -1.f}, std::pair{1.f, 1.f}, std::pair{-1.f, 1.f}}) {
                mesh->positions.push_back({face[0].x + face[1].x * corner.first + face[2].x * corner.second,
                    face[0].y + face[1].y * corner.first + face[2].y * corner.second, face[0].z + face[1].z * corner.first + face[2].z * corner.second});
                mesh->normals.push_back(face[0]);
            }
            mesh->indices.insert(mesh->indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
        }
        material->diffuse = {0.9f, 0.3f, 0.1f, 1.f};
        model->primitives.push_back({mesh, material});
        camera.position = {3.f, 2.5f, 5.f};
        camera.lookAt({0.f, 0.f, 0.f});
        environment.clearColor = {0.1f, 0.2f, 0.4f, 1.f};
        environment.ambient = {0.2f, 0.2f, 0.25f, 1.f};
        environment.lights.push_back(k3::Light::directional({-0.5f, -1.f, -0.4f}));

        const kw::Entity entity = scene.world().create();

        scene.world().add<kuge::Transform3D>(entity, kuge::Transform3D{});
        scene.world().add<kuge::Model3D>(entity, kuge::Model3D{model});
    }
}

Test(render3d_gl, the_gpu_draws)
{
    Frame gpu(kuge::Rendering::OpenGL, cube);

    if (!gpu.available) {
        return;
    }
    Assert(gpu.module->usesOpenGL(), "the GPU draws: %s", gpu.module->rendererName().c_str());
    Assert(gpu.image && gpu.image->width == 96 && gpu.image->height == 64, "a picture of the size of the screen");
    const std::uint8_t* middle = gpu.pixel(48, 32);
    const std::uint8_t* corner = gpu.pixel(2, 2);

    Assert(middle[0] > middle[2] + 40, "the cube is orange in the middle: %d %d %d", middle[0], middle[1], middle[2]);
    Assert(corner[2] > corner[0] + 40, "and the sky is blue around it: %d %d %d", corner[0], corner[1], corner[2]);
}

Test(render3d_gl, auto_picks_the_gpu)
{
    Frame automatic(kuge::Rendering::Auto, cube);

    if (!automatic.available) {
        return;
    }
    // (with a context, Auto is the GPU; without one, the CPU: either way something is drawn)
    Assert(automatic.module->usesOpenGL() == (automatic.client->opengl() != nullptr), "the GPU if there is a context");
    Assert(automatic.pixel(48, 32)[0] > automatic.pixel(48, 32)[2] + 40, "and the cube is drawn");
}

Test(render3d_gl, software_is_asked_for)
{
    Frame cpu(kuge::Rendering::Software, cube);

    Assert(cpu.available, "the CPU works wherever there is a screen");
    Assert(!cpu.module->usesOpenGL() && cpu.client->opengl() == nullptr, "no context was even made");
    Assert(cpu.pixel(48, 32)[0] > cpu.pixel(48, 32)[2] + 40, "and the cube is drawn");
}

Test(render3d_gl, the_gpu_and_the_cpu_agree)
{
    Frame gpu(kuge::Rendering::OpenGL, cube);

    if (!gpu.available) {
        return;
    }
    Frame cpu(kuge::Rendering::Software, cube);
    long total = 0;
    int worst = 0;

    for (std::size_t i = 0; i < gpu.image->rgba.size(); ++i) {
        if (i % 4 == 3) {
            continue;
        }
        const int difference = std::abs(static_cast<int>(gpu.image->rgba[i]) - static_cast<int>(cpu.image->rgba[i]));

        total += difference;
        worst = std::max(worst, difference);
    }
    const double mean = static_cast<double>(total) / (static_cast<double>(gpu.image->rgba.size()) * 0.75);

    Assert(mean < 2.0, "the same picture, give or take: %f of 255 on average", mean);
    std::fprintf(stderr, "(gpu against cpu: %f on average, %d at worst)\n", mean, worst);
}
