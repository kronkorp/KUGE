extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Arena.hpp"
#include "Engine.hpp"
#include "Models.hpp"
#include <cmath>
#include <deque>
#include <functional>
#include <kronk3d/io/GltfLoader.hpp>
#include <kronk3d/io/ObjLoader.hpp>

// NOTE: kronklab test names are limited to 31 characters.

// The rules of Arena, with no network and no screen: the level, the bodies, the bullets, the deaths, the clips.

namespace
{
    using namespace arena;

    // A headless engine with a scene that has the level, the match, and the rules after the physics. One step is one tick.
    struct Level : kuge::Scene
    {
        using kuge::Scene::world;

        explicit Level(bool crates) : m_crates(crates) {}

        void onEnter(void) override
        {
            buildLevel(world());
            world().addResource<Match>();
            world().getResource<Match>().track = [this](kw::Entity, kuge::replication::EntityType type, std::uint32_t) { ++tracked[type]; };
            if (m_crates) {
                buildGame(world());
            }
            addSystem(kw::Schedule::Fixed, kuge::stage::Physics, std::make_unique<Step>([](kw::World& w) { stepWorld(w, TICK_DT); }));
            addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<Step>([](kw::World& w) { stepRules(w); }));
        }

        class Step final : public kw::ISystem
        {
            public:
                explicit Step(std::function<void(kw::World&)> work) : m_work(std::move(work)) {}

                bool handle(kw::World& w) override { m_work(w); return true; }

            private:
                std::function<void(kw::World&)> m_work;
        };

        std::map<kuge::replication::EntityType, int> tracked;
        bool                                         m_crates;
    };

    struct Sim
    {
        kuge::Engine engine;
        Level*       level = nullptr;

        explicit Sim(bool crates = false)
        {
            engine.scenes().change<Level>(crates);
            engine.step(0.0);
            level = static_cast<Level*>(engine.scenes().top());
        }

        void ticks(int count) { for (int i = 0; i < count; ++i) { engine.step(1.0 / 60.0); } }

        kw::World&                world(void) { return level->world(); }
        kuge::Physics3D&          physics(void) { return world().getResource<kuge::Physics3D>(); }
        Vec3                      where(kw::Entity e) { return world().get<kuge::Transform3D>(e).translation; }
        std::string               clip(kw::Entity e) { return world().get<kuge::AnimationState3D>(e).clip; }

        // A player that stands where we say, looking where we say
        kw::Entity player(std::uint32_t id, Vec3 at, float yaw = 0.0f)
        {
            const kw::Entity body = spawnPlayer(world(), id);

            world().get<kuge::Transform3D>(body).translation = at;
            world().get<kuge::Transform3D>(body).rotation = facing(yaw, 0.0f);
            return body;
        }

        static Steer steer(int forward, int strafe, float yaw = 0.0f, bool jump = false, bool fire = false, float pitch = 0.0f)
        {
            Steer s;

            s.forward = static_cast<std::int8_t>(forward);
            s.strafe = static_cast<std::int8_t>(strafe);
            s.jump = jump;
            s.fire = fire;
            s.yaw = quantize(yaw);
            s.pitch = quantize(pitch);
            return s;
        }

        // Holds a steer for some ticks, as a client does (the room applies an input at each tick)
        void hold(kw::Entity body, const Steer& s, int count)
        {
            for (int i = 0; i < count; ++i) {
                steerPlayer(world(), body, s);
                if (s.fire) {
                    fire(world(), body, s);
                }
                ticks(1);
            }
        }
    };

    const float PI = 3.14159265f;

    bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

    template<typename C>
    std::size_t countOf(kw::World& world)
    {
        std::size_t count = 0;

        for ([[maybe_unused]] kw::Entity entity : world.view<C>()) {
            ++count;
        }
        return count;
    }
}

Test(arena, the_spawns_are_free)
{
    Sim sim;

    sim.ticks(2);      // (the bodies are made by the first tick)
    AssertEq(spawns().size(), 8u, "eight places");
    AssertEq(sim.physics().bodyCount(), level().size(), "a body for each piece");
    Assert(level().size() > 20, "a map with some depth: %zu pieces", level().size());
    for (const Spawn& spawn : spawns()) {
        Assert(sim.physics().overlapSphere(spawn.at, 0.5f).empty(), "nothing at the spawn (%f %f)", spawn.at.x, spawn.at.z);
        const auto floor = sim.physics().raycast(spawn.at, {0.0f, -1.0f, 0.0f}, 3.0f);

        Assert(floor && floor->distance < 1.2f, "and a floor under it (%f %f)", spawn.at.x, spawn.at.z);
    }
    for (const Vec3& crate : crates()) {
        Assert(sim.physics().overlapSphere(crate, 0.3f).empty(), "no crate starts inside the level (%f %f)", crate.x, crate.z);
    }
}

Test(arena, a_player_stands_and_walks)
{
    Sim sim;
    const kw::Entity body = sim.player(1, {-18.5f, 1.0f, 10.0f});

    sim.ticks(60);
    Assert(sim.world().get<kuge::Body3D>(body).grounded, "it stands");
    Assert(near(sim.where(body).y, 0.9f, 0.06f), "on its feet: y = %f", sim.where(body).y);
    sim.hold(body, Sim::steer(1, 0), 60);       // a second, toward -z
    Assert(near(sim.where(body).z, 10.0f - WALK_SPEED, 0.5f) && near(sim.where(body).x, -18.5f, 0.05f), "6 m ahead: z = %f", sim.where(body).z);
    sim.hold(body, Sim::steer(0, 1), 30);       // half a second to the right: toward +x
    Assert(near(sim.where(body).x, -18.5f + WALK_SPEED / 2, 0.5f), "and 3 m to the right: x = %f", sim.where(body).x);
    Assert(sim.world().get<kuge::Body3D>(body).grounded, "still on the ground");
}

Test(arena, the_body_turns_with_the_view)
{
    Sim sim;
    const kw::Entity body = sim.player(1, {-18.5f, 1.0f, 10.0f});

    sim.hold(body, Sim::steer(0, 0, PI / 2), 5);
    const auto& rotation = sim.world().get<kuge::Transform3D>(body).rotation;

    Assert(near(rotation.y, std::sin(-PI / 4), 0.01f) && near(rotation.w, std::cos(-PI / 4), 0.01f), "a quarter turn right: y = %f w = %f", rotation.y, rotation.w);
    Assert(near(sim.where(body).x, -18.5f, 0.01f) && near(sim.where(body).z, 10.0f, 0.01f), "without moving");
}

Test(arena, ramps_are_walked_up)
{
    Sim sim;
    // Looking toward +z (yaw = pi), at the foot of the ramp that goes up to the platform in the middle
    const kw::Entity body = sim.player(1, {0.0f, 1.0f, -16.0f}, PI);

    sim.ticks(30);
    sim.hold(body, Sim::steer(1, 0, PI), 150);          // 15 m at 6 m/s: up the ramp and onto the platform
    Assert(sim.where(body).y > 2.7f, "up on the platform (2 m) on its feet (0.9 m): y = %f", sim.where(body).y);
    Assert(sim.world().get<kuge::Body3D>(body).grounded, "standing there");
    Assert(sim.where(body).z > -5.0f, "z = %f", sim.where(body).z);
}

Test(arena, walls_stop_a_player)
{
    Sim sim;
    const kw::Entity body = sim.player(1, {-18.5f, 1.0f, 10.0f});

    sim.hold(body, Sim::steer(1, 0), 600);               // ten seconds toward -z: the wall is at z = -20
    Assert(sim.where(body).z > -20.0f + PLAYER_RADIUS - 0.1f, "it did not go through: z = %f", sim.where(body).z);
}

Test(arena, crates_are_pushed)
{
    Sim sim(true);
    // The crate at (11, 3): a player comes from the east, walking west
    const kw::Entity body = sim.player(1, {17.0f, 1.0f, 3.0f}, -PI / 2);
    kw::Entity crate = 0;

    for (kw::Entity entity : sim.world().view<kuge::Collider3D>()) {
        if (!sim.world().has<kuge::Body3D>(entity) || sim.world().has<Owned>(entity)) {
            continue;
        }
        if (near(sim.where(entity).x, 11.0f, 0.1f) && near(sim.where(entity).z, 3.0f, 0.1f)) {
            crate = entity;
        }
    }
    AssertEq(sim.level->tracked[CRATE], static_cast<int>(crates().size()), "the crates are told to the clients");
    sim.ticks(60);
    sim.hold(body, Sim::steer(1, 0, -PI / 2), 120);
    Assert(sim.where(crate).x < 9.5f, "the crate was pushed west: x = %f", sim.where(crate).x);
    Assert(sim.where(body).x < 10.0f, "and the player followed it: x = %f", sim.where(body).x);
}

Test(arena, pushing_a_crate_is_smooth)
{
    Sim sim(true);
    const kw::Entity body = sim.player(1, {17.0f, 1.0f, 3.0f}, -PI / 2);
    kw::Entity crate = 0;

    for (kw::Entity entity : sim.world().view<kuge::Collider3D>()) {
        if (sim.world().has<kuge::Body3D>(entity) && !sim.world().has<Owned>(entity)
            && near(sim.where(entity).x, 11.0f, 0.1f) && near(sim.where(entity).z, 3.0f, 0.1f)) {
            crate = entity;
        }
    }
    sim.ticks(60);
    float highestPlayer = 0.0f;
    float fastestCrate = 0.0f;
    float tiltedCrate = 0.0f;
    float lowestCrate = 1.0f;
    float highestCrate = 0.0f;

    for (int t = 0; t < 240; ++t) {                  // four seconds against the crate, then past it
        steerPlayer(sim.world(), body, Sim::steer(1, 0, -PI / 2));
        sim.ticks(1);
        const auto& velocity = sim.world().get<kuge::Body3D>(crate).velocity;

        highestPlayer = std::max(highestPlayer, sim.where(body).y);
        fastestCrate = std::max(fastestCrate, std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z));
        tiltedCrate = std::max(tiltedCrate, std::fabs(sim.world().get<kuge::Transform3D>(crate).rotation.x) + std::fabs(sim.world().get<kuge::Transform3D>(crate).rotation.z));
        lowestCrate = std::min(lowestCrate, sim.where(crate).y);
        highestCrate = std::max(highestCrate, sim.where(crate).y);
    }
    Assert(highestPlayer < 1.0f, "the player does not ride up the crate: y = %f", highestPlayer);
    Assert(fastestCrate < WALK_SPEED + 0.1f, "the crate goes no faster than the player that pushes it: %f m/s", fastestCrate);
    Assert(tiltedCrate < 0.1f, "and does not tip over: %f", tiltedCrate);
    Assert(lowestCrate > 0.45f && highestCrate < 0.6f, "it stays on the floor: %f to %f", lowestCrate, highestCrate);
    Assert(sim.where(crate).x < 8.0f, "it was pushed: x = %f", sim.where(crate).x);
    Assert(sim.where(crate).x > 5.4f, "and stopped by the platform, not through it: x = %f", sim.where(crate).x);
}

Test(arena, a_client_predicts_crates)
{
    // A player pushes a crate: the real world is the room's; the predicted one only has the level and the player, and
    // the crate as the client is shown it (six ticks late, the crate of the room as the interpolation has it)
    const auto walk = [](bool withProxies) {
        Sim real(true);
        const kw::Entity pusher = real.player(1, {14.0f, 1.0f, 3.0f}, -PI / 2);
        kw::Entity crate = 0;

        for (kw::Entity entity : real.world().view<kuge::Collider3D>()) {
            if (real.world().has<kuge::Body3D>(entity) && !real.world().has<Owned>(entity)
                && near(real.where(entity).x, 11.0f, 0.1f) && near(real.where(entity).z, 3.0f, 0.1f)) {
                crate = entity;
            }
        }
        kw::World predicted;
        kw::World shown;
        Proxies proxies;

        buildLevel(predicted);
        const kw::Entity self = buildPlayer(predicted, {14.0f, 1.0f, 3.0f}, -PI / 2);
        const kw::Entity drawn = shown.create();

        shown.add<kuge::replication::Replicated>(drawn, kuge::replication::Replicated{7, CRATE, 0});
        shown.add<kuge::Transform3D>(drawn, real.world().get<kuge::Transform3D>(crate));
        shown.add<kuge::Body3D>(drawn, kuge::Body3D{});
        std::deque<std::pair<kuge::Transform3D, kuge::Body3D>> late;

        real.ticks(30);
        for (int t = 0; t < 120; ++t) {
            steerPlayer(real.world(), pusher, Sim::steer(1, 0, -PI / 2));
            real.ticks(1);
            late.push_back({real.world().get<kuge::Transform3D>(crate), real.world().get<kuge::Body3D>(crate)});
            if (late.size() > 6) {
                shown.get<kuge::Transform3D>(drawn) = late.front().first;
                shown.get<kuge::Body3D>(drawn) = late.front().second;
                late.pop_front();
            }
            steerPlayer(predicted, self, Sim::steer(1, 0, -PI / 2));
            if (withProxies) {
                syncProxies(proxies, predicted, shown, 1);
            }
            stepWorld(predicted, TICK_DT);
        }
        return std::fabs(real.where(pusher).x - predicted.get<kuge::Transform3D>(self).translation.x);
    };

    Assert(walk(true) < 0.3f, "with the crate, the predicted player is where the real one is: %f m apart", walk(true));
    Assert(walk(false) > 0.7f, "without it, it walks through the crate (this is what the proxies are for): %f m", walk(false));
}

Test(arena, shots_hurt_and_kill)
{
    Sim sim;
    // Two players on a line along the west wall, A looking at B (toward -z)
    const kw::Entity a = sim.player(1, {-18.5f, 1.0f, 10.0f});
    const kw::Entity b = sim.player(2, {-18.5f, 1.0f, 0.0f});

    sim.ticks(30);
    AssertEq(sim.world().get<Health>(b).points, HEALTH, "full health");
    sim.hold(a, Sim::steer(0, 0, 0.0f, false, true), 40);       // a shot every 12 ticks: a few have hit
    Assert(sim.world().get<Health>(b).points < HEALTH, "B is hurt: %d", sim.world().get<Health>(b).points);
    AssertEq(sim.world().get<Health>(a).points, HEALTH, "A is not (a bullet does not hit who fires it)");
    sim.hold(a, Sim::steer(0, 0, 0.0f, false, true), 100);
    Assert(!playerOf(sim.world(), 2), "B is dead after four hits");
    AssertEq(countOf<Corpse>(sim.world()), 1u, "its body lies there");
    AssertEq(sim.level->tracked[CORPSE], 1, "and the clients are told");
    AssertEq(sim.world().getResource<Match>().frags[1], 1, "A has a frag");
    AssertEq(sim.world().get<Score>(a).frags, 1, "that its score shows");
    sim.ticks(RESPAWN_DELAY);
    const auto back = playerOf(sim.world(), 2);

    Assert(back.has_value(), "B comes back");
    AssertEq(sim.world().get<Health>(*back).points, HEALTH, "with all its health");
    AssertEq(countOf<Corpse>(sim.world()), 0u, "and the body is gone");
}

Test(arena, bullets_stop_at_walls)
{
    Sim sim;
    const kw::Entity a = sim.player(1, {-18.5f, 1.0f, 10.0f});

    sim.ticks(30);
    sim.hold(a, Sim::steer(0, 0, 0.0f, false, true), 1);
    AssertEq(countOf<Projectile>(sim.world()), 1u, "a bullet");
    sim.ticks(30);
    AssertEq(countOf<Projectile>(sim.world()), 1u, "in the air (30 m take 40 ticks)");
    sim.ticks(30);
    AssertEq(countOf<Projectile>(sim.world()), 0u, "stopped by the wall, long before it would expire");
}

Test(arena, bullets_go_up_and_down)
{
    Sim sim;
    const kw::Entity a = sim.player(1, {-18.5f, 1.0f, 10.0f});
    const kw::Entity b = sim.player(2, {-18.5f, 1.0f, 0.0f});

    sim.ticks(30);
    // Aimed over B's head: it misses (a capsule is 1.8 m, and the shot is 10 m away: 0.2 rad is 2 m up)
    sim.hold(a, Sim::steer(0, 0, 0.0f, false, true, 0.2f), 1);
    sim.ticks(40);
    AssertEq(sim.world().get<Health>(b).points, HEALTH, "a shot aimed too high misses");
    sim.ticks(FIRE_COOLDOWN);
    sim.hold(a, Sim::steer(0, 0, 0.0f, false, true, 0.0f), 1);
    sim.ticks(40);
    Assert(sim.world().get<Health>(b).points < HEALTH, "and one aimed at it hits");
}

Test(arena, the_match_ends)
{
    Sim sim;
    const kw::Entity a = sim.player(1, {-18.5f, 1.0f, 10.0f});
    const kw::Entity b = sim.player(2, {-18.5f, 1.0f, 0.0f});

    (void)b;
    sim.ticks(30);
    Assert(!finished(sim.world()), "not yet");
    for (int round = 0; round < FRAG_LIMIT && !finished(sim.world()); ++round) {
        // A shoots B until it dies, then waits for it to come back
        for (int t = 0; t < 400 && playerOf(sim.world(), 2).has_value() && !finished(sim.world()); ++t) {
            sim.hold(a, Sim::steer(0, 0, 0.0f, false, true), 1);
        }
        if (const auto back = playerOf(sim.world(), 2); !back) {
            sim.ticks(RESPAWN_DELAY + 1);
        }
        if (const auto back = playerOf(sim.world(), 2)) {
            // (it comes back where the rules put it: put it in front of A again)
            sim.world().get<kuge::Transform3D>(*back).translation = {-18.5f, 1.0f, 0.0f};
            sim.ticks(10);
        }
    }
    Assert(finished(sim.world()), "after %d frags the match is over", FRAG_LIMIT);
    AssertEq(sim.world().getResource<Match>().winner, 1u, "A won");
}

Test(arena, clips_follow_what_it_does)
{
    Sim sim;
    const kw::Entity a = sim.player(1, {-18.5f, 1.0f, 10.0f});

    sim.ticks(60);
    Assert(sim.clip(a) == "idle", "standing: %s", sim.clip(a).c_str());
    sim.hold(a, Sim::steer(1, 0), 10);
    Assert(sim.clip(a) == "run", "walking: %s", sim.clip(a).c_str());
    sim.hold(a, Sim::steer(0, 0), 5);
    Assert(sim.clip(a) == "idle", "stopped: %s", sim.clip(a).c_str());
    sim.hold(a, Sim::steer(0, 0, 0.0f, true), 1);
    sim.hold(a, Sim::steer(0, 0), 8);
    Assert(sim.clip(a) == "jump", "going up: %s", sim.clip(a).c_str());
    sim.hold(a, Sim::steer(0, 0), 32);
    Assert(sim.clip(a) == "falling", "coming down: %s", sim.clip(a).c_str());
    sim.hold(a, Sim::steer(0, 0), 90);
    Assert(sim.clip(a) == "idle", "landed: %s", sim.clip(a).c_str());
    const auto serial = sim.world().get<kuge::AnimationState3D>(a).serial;

    sim.hold(a, Sim::steer(0, 0, 0.0f, false, true), 1);
    Assert(sim.clip(a) == "shooting_standing" && sim.world().get<kuge::AnimationState3D>(a).serial != serial, "firing: %s", sim.clip(a).c_str());
    sim.ticks(20);
    sim.hold(a, Sim::steer(1, 0, 0.0f, false, true), 3);
    Assert(sim.clip(a) == "shooting", "firing while running: %s", sim.clip(a).c_str());
    sim.hold(a, Sim::steer(0, 0), SHOT_CLIP - 6);
    Assert(sim.clip(a) == "shooting_standing", "the shot's clip plays to its end (and the player stopped): %s", sim.clip(a).c_str());
    sim.hold(a, Sim::steer(0, 0), 12);
    Assert(sim.clip(a) == "idle", "and back: %s", sim.clip(a).c_str());
}

Test(arena, assets_are_there)
{
    const std::filesystem::path dir = ARENA_ASSETS_DIR;
    auto robot = k3::GltfLoader::load(dir / "robot.glb");

    Assert(robot.operator bool(), "robot.glb loads: %s", robot ? "" : robot.error().c_str());
    for (const char* clip : {"idle", "run", "jump", "falling", "shooting", "shooting_standing"}) {
        Assert(robot->findAnimation(clip) >= 0, "the clip %s", clip);
    }
    bool glow = false;

    for (const auto& primitive : robot->primitives) {
        glow = glow || (primitive.material && primitive.material->name == "player_glow");
        Assert(primitive.skin >= 0, "skinned");
    }
    Assert(glow, "its lights are a material named player_glow (painted with the colour of the player)");

    // What the client makes of it: our size, our direction, the colour of a player, and a clip for dying
    const auto player = arena::makeRobot(*robot, {1.0f, 0.0f, 1.0f, 1.0f});

    Assert(player->findAnimation("death") >= 0, "a clip for dying is added");
    Assert(robot->findAnimation("death") < 0, "(to the copy: the file is not touched)");
    const auto& root = player->nodes[0];

    const float ratio = root.local.scale.y / robot->nodes[0].local.scale.y;

    Assert(root.parent < 0 && near(ratio * 2.12f, 1.75f, 0.1f), "shrunk from 2.12 m to about the height of the capsule: %f", ratio * 2.12f);
    Assert(near(root.local.translation.y, -PLAYER_HEIGHT / 2, 0.01f), "its feet at the bottom of the capsule: %f", root.local.translation.y);
    Assert(near(std::fabs(root.local.rotation.y), 1.0f, 0.01f), "turned to face -z");
    auto crate = k3::ObjLoader::load(dir / "crate.obj");

    Assert(crate.operator bool(), "crate.obj loads: %s", crate ? "" : crate.error().c_str());
    Assert(crate->primitives.size() == 1 && crate->primitives[0].material && crate->primitives[0].material->diffuseMap, "with its texture");
    for (const char* texture : {"floor.png", "wall.png", "platform.png"}) {
        Assert(k3::Texture::load(dir / texture) != nullptr, "%s loads", texture);
    }
}
