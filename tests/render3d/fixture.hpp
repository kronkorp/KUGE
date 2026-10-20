#pragma once

#include "ClientModule.hpp"
#include "Engine.hpp"
#include "Render3DModule.hpp"
#include "Scene3D.hpp"
#include "backend/dummy/DummyBackend.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unistd.h>

// A windowed engine with the dummy backend, the 3D renderer and an empty 3D scene, and a folder for files

namespace render3d
{
    // A folder of the test, removed with it
    struct Folder
    {
        std::filesystem::path path;

        explicit Folder(const char* name)
            : path(std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_" + name))
        {
            std::filesystem::create_directories(path);
        }
        ~Folder() { std::filesystem::remove_all(path); }

        // The date is set too, so that a test does not depend on the clock's grain
        std::filesystem::path write(const char* name, const std::string& text, int seconds = 0) const
        {
            const auto file = path / name;

            std::ofstream(file, std::ios::binary) << text;
            std::filesystem::last_write_time(file, std::filesystem::file_time_type{} + std::chrono::seconds(1000 + seconds));
            return file;
        }
    };

    struct Scene : kuge::Scene3D
    {
        void onEnter(void) override { installClient3DSystems(); }
        using kuge::Scene::world;
    };

    struct Fixture
    {
        kuge::DummyBackend    dummy;
        kuge::Engine          engine;
        kuge::Render3DModule* models;

        explicit Fixture(kuge::Render3DConfig config = {})
            : dummy(kuge::makeDummyBackend({64.0f, 48.0f})),
              engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed})
        {
            engine.addModule<kuge::ClientModule>(std::move(dummy.backend));
            models = &engine.addModule<kuge::Render3DModule>(config);
            engine.scenes().change<Scene>();
            engine.step(0.0);
        }

        kw::World& world(void) { return static_cast<Scene*>(engine.scenes().top())->world(); }
    };

}
