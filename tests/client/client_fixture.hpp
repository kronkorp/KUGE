#pragma once

#include "ClientModule.hpp"
#include "ClientScene.hpp"
#include "Engine.hpp"
#include "backend/dummy/DummyBackend.hpp"
#include <functional>

// A windowed engine with the dummy backend, and a scene to play with

struct TestScene : kuge::ClientScene
{
    using kuge::Scene::world;

    explicit TestScene(std::function<void(TestScene&)> setup = {}) : m_setup(std::move(setup)) {}

    void onEnter(void) override
    {
        installClientSystems();
        if (m_setup) {
            m_setup(*this);
        }
    }

    using kuge::Scene::addSystem;

    std::function<void(TestScene&)> m_setup;
};

struct Fixture
{
    kuge::DummyBackend   dummy;
    kuge::Engine         engine;
    kuge::ClientModule*  client;
    TestScene*           scene = nullptr;

    explicit Fixture(std::function<void(TestScene&)> setup = {}, kuge::Vec2 size = {640.0f, 480.0f})
        : dummy(kuge::makeDummyBackend(size)),
          engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed}),
          client(&engine.addModule<kuge::ClientModule>(std::move(dummy.backend)))
    {
        engine.scenes().change<TestScene>(std::move(setup));
        engine.step(0.0);
        scene = static_cast<TestScene*>(engine.scenes().top());
    }

    //! Runs one loop that lasted seconds, and gives back what was drawn
    const std::vector<kuge::DummyRenderer::Call>& frame(double seconds = 0.0)
    {
        engine.step(seconds);
        return dummy.renderer->lastFrame();
    }
};
