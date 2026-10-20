extern "C" {
    #include "kronklab/kronklab.h"
}
#include "VisibilityFilter.hpp"
#include "rep_fixture.hpp"
#include <cmath>
#include <map>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace kuge::replication;
    using Vec3 = VisibilityConfig::Vec3;

    // The viewer is the client: eyes at (-5, 1.7, 0). Entities lie on the ground at (x, 0, z) = their Transform2D (x, y).
    // A wall at x = 0 for |z| < 2. The game's rules: a ship with team 2 is an ally (always sent).
    struct Arena
    {
        RepSim                       sim;
        std::map<NetworkId, kw::Entity> entityOf;
        std::map<NetworkId, Vec3>    velocity;
        Vec3                         eye{-5.0f, 1.7f, 0.0f};
        VisibilityFilter             visibility;

        explicit Arena(double lookAhead = 0.1, std::uint32_t grace = 6)
            : visibility(config(lookAhead, grace))
        {
            sim.server->setFilter(visibility.filter());
        }

        VisibilityConfig config(double lookAhead, std::uint32_t grace)
        {
            VisibilityConfig c;

            c.viewDistance = 50.0f;
            c.graceTicks = grace;
            c.lookAhead = lookAhead;
            c.viewer = [this](kuge::net::ConnectionId) { return std::optional<VisibilityConfig::Viewer>(VisibilityConfig::Viewer{eye, {}}); };
            c.position = [this](NetworkId id) -> std::optional<VisibilityConfig::Placed> {
                const auto found = entityOf.find(id);

                if (found == entityOf.end()) {
                    return std::nullopt;
                }
                const auto& at = sim.serverWorld.get<kuge::Transform2D>(found->second).position;

                return VisibilityConfig::Placed{Vec3{at.x, 0.0f, at.y}, velocity.count(id) ? velocity[id] : Vec3{}};
            };
            c.clear = [](const Vec3& from, const Vec3& to) {
                if ((from.x < 0.0f) == (to.x < 0.0f)) {
                    return true;   // the same side of the wall
                }
                const float t = (0.0f - from.x) / (to.x - from.x);
                const float z = from.z + (to.z - from.z) * t;

                return std::fabs(z) >= 2.0f;   // through the gap, or round the end
            };
            c.always = [this](kuge::net::ConnectionId, NetworkId id, const EntityRecord&) {
                const auto found = entityOf.find(id);

                return found != entityOf.end() && sim.serverWorld.get<Team>(found->second).id == 2;
            };
            return c;
        }

        NetworkId add(float x, float z, std::uint8_t team = 1)
        {
            const kw::Entity entity = sim.spawn(SHIP, x, z, 100, team);
            const NetworkId id = sim.serverWorld.get<Replicated>(entity).id;

            entityOf[id] = entity;
            return id;
        }

        void run(int ticks)
        {
            for (int i = 0; i < ticks; ++i) {
                visibility.newTick(sim.tick + 1);
                sim.step();
            }
        }

        bool clientHas(NetworkId id) { return sim.client->entity(id).has_value(); }
    };
}

Test(visibility, a_wall_hides)
{
    Arena arena;
    const NetworkId visible = arena.add(-8.0f, 0.0f);
    const NetworkId hidden = arena.add(5.0f, 0.0f);          // behind the wall
    const NetworkId around = arena.add(5.0f, 10.0f);         // the same side, but the ray goes round the end of the wall
    const NetworkId ally = arena.add(6.0f, 0.5f, 2);         // behind the wall, an ally: the game's rule
    const NetworkId far = arena.add(-70.0f, 0.0f);           // beyond the view distance

    arena.run(20);
    Assert(arena.clientHas(visible) && arena.clientHas(around), "what can be seen is sent");
    Assert(!arena.clientHas(hidden), "what is behind the wall is not");
    Assert(arena.clientHas(ally), "an ally is, by the game's rule");
    Assert(!arena.clientHas(far), "nor what is too far");
    AssertEq(arena.sim.clientCount(), 3u, "three entities in all");
    const auto& stats = arena.visibility.stats();

    Assert(stats.hidden > 0 && stats.tooFar > 0 && stats.seen > 0 && stats.rays > 0, "the filter counts what it did");
    Assert(arena.sim.server->stats().entitiesFiltered > 0 && arena.sim.server->stats().entitiesSent > 0, "and so does the server: sent %llu, filtered %llu",
        static_cast<unsigned long long>(arena.sim.server->stats().entitiesSent), static_cast<unsigned long long>(arena.sim.server->stats().entitiesFiltered));
}

Test(visibility, hidden_is_not_in_the_bytes)
{
    Arena arena;
    const NetworkId hidden = arena.add(5.0f, 0.0f);

    arena.add(-8.0f, 0.0f);
    arena.run(10);
    // Not just ignored by the client: the client has been sent nothing about it
    Assert(!arena.clientHas(hidden), "the client does not have it");
    AssertEq(arena.sim.client->stats().spawned, 1u, "it never spawned there: no record of it arrived");
    const WorldState onServer = captureState(arena.sim.serverWorld, arena.sim.registry);

    Assert(onServer.count(hidden) == 1 && onServer.size() == 2, "(both are in the server's world)");
}

Test(visibility, appears_before_it_can_be_seen)
{
    // The entity runs towards the gap at 20 m/s. At z = 3.5 the straight way is blocked; 0.1 s later it will be at z = 5.5, in view.
    Arena early(0.1);
    Arena late(0.0);
    const NetworkId a = early.add(5.0f, 3.5f);
    const NetworkId b = late.add(5.0f, 3.5f);

    early.velocity[a] = Vec3{0.0f, 0.0f, 20.0f};
    late.velocity[b] = Vec3{0.0f, 0.0f, 20.0f};
    early.run(10);
    late.run(10);
    Assert(early.clientHas(a), "with a look ahead, it is sent before it is in view");
    Assert(!late.clientHas(b), "without, it is not: the straight way is blocked");
}

Test(visibility, grace_keeps_it_a_while)
{
    Arena arena(0.0, 8);
    const NetworkId walker = arena.add(-8.0f, 0.0f);

    arena.run(10);
    Assert(arena.clientHas(walker), "in view");
    arena.sim.serverWorld.get<kuge::Transform2D>(arena.entityOf[walker]).position = {5.0f, 0.0f};   // behind the wall at once
    arena.run(4);
    Assert(arena.clientHas(walker), "hidden for 4 ticks: still sent (grace of 8)");
    arena.run(12);
    Assert(!arena.clientHas(walker), "after the grace it is gone");
}

namespace
{
    // How many different values of Health the client sees while the server changes it at every tick for 40 ticks
    std::size_t valuesSeen(RepSim& sim)
    {
        std::vector<int> seen;
        const kw::Entity e = sim.spawn(SHIP, 0, 0, 0);

        sim.run(6);
        for (int i = 1; i <= 40; ++i) {
            sim.serverWorld.get<Health>(e).points = i;
            sim.step();
            if (const auto client = sim.client->entity(1)) {
                const int now = sim.clientWorld.get<Health>(*client).points;

                if (seen.empty() || seen.back() != now) {
                    seen.push_back(now);
                }
            }
        }
        return seen.size();
    }
}

Test(visibility, far_things_are_slower)
{
    RepSim normal;
    RepSim slow;

    slow.server->setRate([](kuge::net::ConnectionId, NetworkId, const EntityRecord&) { return 4u; });
    const std::size_t all = valuesSeen(normal);
    const std::size_t some = valuesSeen(slow);

    AssertEq(all, 20u, "a snapshot every 2 ticks: 20 values in 40 ticks");
    Assert(some >= 4 && some <= 6, "refreshed every 4th snapshot: about 5 values, got %zu", some);
    Assert(slow.server->stats().entitiesSkipped > 0 && normal.server->stats().entitiesSkipped == 0, "the skipped ones are counted");
    // A new entity is not made to wait: the client has no record of it to keep
    RepSim fresh;

    fresh.server->setRate([](kuge::net::ConnectionId, NetworkId, const EntityRecord&) { return 1000u; });
    fresh.run(3);
    fresh.spawn(SHIP, 1, 1);
    fresh.run(4);
    AssertEq(fresh.clientCount(), 1u, "an entity the client does not have yet is sent at once, whatever its rate");
}
