#pragma once

#include "Engine.hpp"
#include "Physics3D.hpp"
#include <cmath>
#include <functional>

// A headless engine with a scene that has 3D physics. One step is one tick.

namespace
{
    using Vec3 = k3::Math::Vector3f;

    // A headless engine with a scene that has 3D physics. One step is one tick.
    struct Level : kuge::Scene
    {
        using kuge::Scene::world;

        explicit Level(std::function<void(Level&)> build) : m_build(std::move(build)) {}

        void onEnter(void) override
        {
            kuge::installPhysics3D(setup());
            if (m_build) {
                m_build(*this);
            }
        }

        std::function<void(Level&)> m_build;
    };

    struct Sim
    {
        kuge::Engine engine;
        Level*       level = nullptr;

        explicit Sim(std::function<void(Level&)> build = {})
        {
            engine.scenes().change<Level>(std::move(build));
            engine.step(0.0);
            level = static_cast<Level*>(engine.scenes().top());
        }

        void ticks(int count) { for (int i = 0; i < count; ++i) { engine.step(1.0 / 60.0); } }

        kw::World&         world(void) { return level->world(); }
        kuge::Physics3D&   physics(void) { return world().getResource<kuge::Physics3D>(); }
        Vec3               where(kw::Entity e) { return world().get<kuge::Transform3D>(e).translation; }
        kuge::Body3D&      body(kw::Entity e) { return world().get<kuge::Body3D>(e); }
    };

    bool near(float a, float b, float tolerance = 0.05f) { return std::fabs(a - b) <= tolerance; }

    kw::Entity thing(kw::World& world, Vec3 at, kuge::Collider3D collider)
    {
        const kw::Entity entity = world.create();
        kuge::Transform3D transform;

        transform.translation = at;
        world.add<kuge::Transform3D>(entity, transform);
        world.add<kuge::Collider3D>(entity, collider);
        return entity;
    }

    // The top of this floor is the plane y = 0
    kw::Entity floor(kw::World& world, kuge::Collider3D collider = kuge::Collider3D::box(50.0f, 1.0f, 50.0f))
    {
        return thing(world, {0.0f, -0.5f, 0.0f}, collider);
    }

    kw::Entity crate(kw::World& world, Vec3 at, kuge::Collider3D collider = kuge::Collider3D::box(1.0f, 1.0f, 1.0f),
        kuge::Body3D body = {})
    {
        const kw::Entity entity = thing(world, at, collider);

        world.add<kuge::Body3D>(entity, body);
        return entity;
    }
}

