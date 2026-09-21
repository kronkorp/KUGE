#pragma once

#include "Engine.hpp"
#include "Physics.hpp"
#include <cmath>
#include <cstring>
#include <functional>

// A headless engine with a scene that has physics. One step is one tick.

struct Level : kuge::Scene
{
    using kuge::Scene::world;
    using kuge::Scene::addSystem;

    Level(std::function<void(Level&)> build, kuge::PhysicsConfig config)
        : m_build(std::move(build)), m_config(config) {}

    void onEnter(void) override
    {
        kuge::installPhysics(setup(), m_config);
        if (m_build) {
            m_build(*this);
        }
    }

    std::function<void(Level&)> m_build;
    kuge::PhysicsConfig         m_config;
};

struct Sim
{
    kuge::Engine engine;
    Level*       level = nullptr;

    explicit Sim(std::function<void(Level&)> build = {}, kuge::PhysicsConfig config = {})
    {
        engine.scenes().change<Level>(std::move(build), config);
        engine.step(0.0);
        level = static_cast<Level*>(engine.scenes().top());
    }

    void ticks(int count)
    {
        for (int i = 0; i < count; ++i) {
            engine.step(1.0 / 60.0);
        }
    }

    kw::World&        world(void) { return level->world(); }
    kuge::Physics2D&  physics(void) { return world().getResource<kuge::Physics2D>(); }
    kuge::Vec2        where(kw::Entity e) { return world().get<kuge::Transform2D>(e).position; }
    kuge::Body&       body(kw::Entity e) { return world().get<kuge::Body>(e); }
};

// A thing that does not move
inline kw::Entity wall(kw::World& world, kuge::Vec2 position, kuge::Collider collider)
{
    const kw::Entity entity = world.create();

    world.add<kuge::Transform2D>(entity, kuge::Transform2D{position});
    world.add<kuge::Collider>(entity, collider);
    return entity;
}

// A thing that moves
inline kw::Entity mover(kw::World& world, kuge::Vec2 position, kuge::Collider collider, kuge::Body body)
{
    const kw::Entity entity = wall(world, position, collider);

    world.add<kuge::Body>(entity, body);
    return entity;
}

inline kuge::Body kinematic(kuge::Vec2 velocity)
{
    kuge::Body body;

    body.type = kuge::Body::Type::Kinematic;
    body.velocity = velocity;
    return body;
}

inline kuge::Body dynamic(kuge::Vec2 velocity = {})
{
    kuge::Body body = kinematic(velocity);

    body.type = kuge::Body::Type::Dynamic;
    return body;
}

inline bool near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) < tolerance; }
