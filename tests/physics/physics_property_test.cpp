extern "C" {
    #include "kronklab/kronklab.h"
}
#include "physics_fixture.hpp"
#include <cstdint>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using kuge::Collider;
    using kuge::Vec2;

    struct Random
    {
        std::uint32_t state;
        float next(float low, float high)
        {
            state = state * 1664525u + 1013904223u;
            return low + (high - low) * static_cast<float>(state >> 8) / static_cast<float>(1 << 24);
        }
    };

    struct Scenario
    {
        std::vector<kw::Entity> movers;
        std::vector<kw::Entity> walls;
    };

    // A field of walls, and things that move about in it
    Sim makeScenario(std::uint32_t seed, Scenario& scenario, int walls, int movers)
    {
        return Sim([&, seed, walls, movers](Level& level) {
            Random random{seed};
            auto& w = level.world();

            for (int i = 0; i < walls; ++i) {
                const Collider shape = i % 4 == 3 ? Collider::circle(random.next(8.0f, 24.0f))
                    : Collider::box(random.next(10.0f, 80.0f), random.next(10.0f, 80.0f));

                scenario.walls.push_back(wall(w, {random.next(-300.0f, 300.0f), random.next(-300.0f, 300.0f)}, shape));
            }
            for (int i = 0; i < movers; ++i) {
                kuge::Body body = i % 2 == 0 ? dynamic() : kinematic({});

                body.velocity = {random.next(-400.0f, 400.0f), random.next(-400.0f, 400.0f)};
                scenario.movers.push_back(mover(w, {random.next(-300.0f, 300.0f), random.next(-300.0f, 300.0f)},
                    Collider::box(random.next(6.0f, 20.0f), random.next(6.0f, 20.0f)), body));
            }
        }, kuge::PhysicsConfig{.gravity = {0.0f, 600.0f}});
    }

    // Overlap by more than the rounding of a float
    bool sunkIn(Sim& sim, kw::Entity mover, kw::Entity other)
    {
        const auto& t = sim.world().get<kuge::Transform2D>(mover);
        const auto& c = sim.world().get<Collider>(mover);
        const auto& ot = sim.world().get<kuge::Transform2D>(other);
        const auto& oc = sim.world().get<Collider>(other);
        const kuge::Aabb a = kuge::Aabb::fromCenter(t.position + c.offset, c.size * 0.5f);
        const kuge::Aabb b = oc.shape == Collider::Shape::Circle
            ? kuge::Aabb::fromCenter(ot.position + oc.offset, {oc.size.x, oc.size.x})
            : kuge::Aabb::fromCenter(ot.position + oc.offset, oc.size * 0.5f);
        const float x = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
        const float y = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);

        if (oc.shape == Collider::Shape::Circle) {
            return kuge::overlaps(a.expanded(-0.05f), kuge::Circle{ot.position + oc.offset, oc.size.x});
        }
        return x > 0.05f && y > 0.05f;
    }
}

// Whatever the bodies do, none of them that started free ends up inside a wall
Test(physics_props, free_bodies_stay_free)
{
    int checked = 0;

    for (std::uint32_t seed = 10; seed < 16; ++seed) {
        Scenario scenario;
        Sim sim = makeScenario(seed, scenario, 20, 8);
        std::vector<bool> startsFree;

        for (kw::Entity body : scenario.movers) {
            bool free = true;

            for (kw::Entity other : scenario.walls) {
                free = free && !sunkIn(sim, body, other);
            }
            startsFree.push_back(free);
        }
        for (int tick = 0; tick < 300; ++tick) {
            sim.ticks(1);
            for (std::size_t i = 0; i < scenario.movers.size(); ++i) {
                if (!startsFree[i]) {
                    continue;
                }
                for (kw::Entity other : scenario.walls) {
                    Assert(!sunkIn(sim, scenario.movers[i], other), "seed %u tick %d: body %zu is inside a wall", seed, tick, i);
                    ++checked;
                }
            }
        }
    }
    Assert(checked > 10000, "enough was checked: %d", checked);
}

Test(physics_props, same_world_same_result)
{
    auto run = [](std::vector<Vec2>& positions) {
        Scenario scenario;
        Sim sim = makeScenario(77, scenario, 30, 12);

        sim.ticks(400);
        for (kw::Entity body : scenario.movers) {
            positions.push_back(sim.where(body));
        }
    };
    std::vector<Vec2> first;
    std::vector<Vec2> second;

    run(first);
    run(second);
    AssertEq(first.size(), 12, "12 bodies");
    // Exactly the same, bit for bit: not "close"
    AssertEq(std::memcmp(first.data(), second.data(), first.size() * sizeof(Vec2)), 0, "two runs give the same positions");
}

Test(physics_props, storage_order_is_not_used)
{
    // The same bodies, added in another order, then some removed and added:
    // the ECS stores them differently, the result must not depend on it
    auto run = [](bool churn) {
        kw::Entity a = 0, b = 0, c = 0;
        Sim sim([&](Level& level) {
            auto& w = level.world();

            wall(w, {0.0f, 100.0f}, Collider::box(400.0f, 20.0f));
            if (churn) {
                for (int i = 0; i < 5; ++i) {
                    const kw::Entity temporary = mover(w, {1000.0f + static_cast<float>(i), 0.0f}, Collider::box(2.0f, 2.0f), dynamic());

                    w.remove(temporary);
                }
            }
            a = mover(w, {-50.0f, 0.0f}, Collider::box(10.0f, 10.0f), dynamic({30.0f, 0.0f}));
            b = mover(w, {0.0f, -40.0f}, Collider::box(12.0f, 12.0f), dynamic({-20.0f, 0.0f}));
            c = mover(w, {50.0f, -80.0f}, Collider::box(8.0f, 8.0f), dynamic({0.0f, 0.0f}));
        });

        sim.ticks(200);
        return std::vector<Vec2>{sim.where(a), sim.where(b), sim.where(c)};
    };
    const auto plain = run(false);
    const auto churned = run(true);

    AssertEq(std::memcmp(plain.data(), churned.data(), plain.size() * sizeof(Vec2)), 0, "the same, whatever the history of the World");
}
