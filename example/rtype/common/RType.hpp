#pragma once

// R-Type, the small one: ships shoot waves of enemies in an arena, on one screen, up to four players.
//
// This directory is what the server and the client share: the vocabulary (types, messages, what is
// replicated), the arena, and the rules. The server (rooms) and the client (a window) are separate
// programs that include it, and a third one runs both in a single process. None of them changes the
// engine: this is the check that the modules make any game.

#include "CommonComponents.hpp"
#include "PhysicsComponents.hpp"
#include "Physics2D.hpp"
#include "ReplicationServer.hpp"
#include "Transform2D.hpp"
#include "Wire.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace rtype
{

    constexpr float ARENA_W = 640.0f;
    constexpr float ARENA_H = 360.0f;
    constexpr float SHIP_SPEED = 150.0f;       // pixels per second
    constexpr float BULLET_SPEED = 360.0f;
    constexpr float ENEMY_SPEED = 55.0f;
    constexpr kuge::Vec2 SHIP_SIZE{16.0f, 10.0f};
    constexpr kuge::Vec2 BULLET_SIZE{6.0f, 3.0f};
    constexpr kuge::Vec2 ENEMY_SIZE{14.0f, 14.0f};
    constexpr double TICK_DT = 1.0 / 60.0;

    // What kinds of entities the server tells the clients about
    constexpr kuge::replication::EntityType SHIP = 1;
    constexpr kuge::replication::EntityType BULLET = 2;
    constexpr kuge::replication::EntityType ENEMY = 3;

    // -- What a player does: sent by the client, applied by the room, predicted by the client -------
    struct Steer
    {
        KUGE_MESSAGE(Steer, dx, dy, fire)
        std::int8_t dx = 0;
        std::int8_t dy = 0;
        bool        fire = false;

        bool operator==(const Steer&) const = default;
    };

    // -- Components ---------------------------------------------------------------------------------
    struct Health { int points = 0; };                          //!< Replicated when it changes
    struct Score  { int points = 0; };                          //!< Replicated when it changes
    struct Slot   { std::uint8_t color = 0; };                  //!< Replicated once: the colour of a ship
    // The server alone knows these
    struct Gun     { std::uint32_t cooldown = 0; };
    struct Owned   { std::uint32_t player = 0; };               //!< The network id of the player a ship or a bullet belongs to
    struct Bullet  { kuge::Vec2 velocity; };
    struct Enemy   { float baseY = 0.0f; float phase = 0.0f; float amplitude = 25.0f; };

    //! How long and how hard a game is (a test makes it short)
    struct Settings
    {
        std::uint32_t waveSize      = 30;    //!< Enemies in a game; the players win when they are all dead
        std::uint32_t spawnEvery    = 45;    //!< Ticks between two enemies
        int           shipHealth    = 5;
        int           enemyHealth   = 2;
        std::uint32_t fireCooldown  = 12;    //!< Ticks between two shots of a ship
        float         enemySpeed    = ENEMY_SPEED;   //!< Pixels per second
    };

    inline Settings& settings(void)
    {
        static Settings value;

        return value;
    }

    // -- What is replicated: the same list for the room and the clients -------------------------------
    inline kuge::replication::ReplicationRegistry makeRegistry(void)
    {
        using namespace kuge::replication;
        ReplicationRegistry registry;

        registerTransform2D(registry, Replicate::Interpolated, true);     // the player predicts its ship
        registerBody(registry, Replicate::OnChange, true);
        registry.component<Health>("Health", Replicate::OnChange,
            [](kuge::ByteWriter& out, const Health& h) { out.write<std::int32_t>(h.points); },
            [](kuge::ByteReader& in) { return Health{in.read<std::int32_t>()}; });
        registry.component<Score>("Score", Replicate::OnChange,
            [](kuge::ByteWriter& out, const Score& s) { out.write<std::int32_t>(s.points); },
            [](kuge::ByteReader& in) { return Score{in.read<std::int32_t>()}; });
        registry.component<Slot>("Slot", Replicate::OnSpawn,
            [](kuge::ByteWriter& out, const Slot& s) { out.write(s.color); },
            [](kuge::ByteReader& in) { return Slot{in.read<std::uint8_t>()}; });
        return registry;
    }

    // -- The arena: walls around the screen, and the ships that move in it -----------------------------
    inline void buildArena(kw::World& world)
    {
        world.addResource<kuge::Physics2D>(kuge::PhysicsConfig{});
        const auto wall = [&world](float x, float y, float w, float h) {
            const kw::Entity e = world.create();

            world.add<kuge::Transform2D>(e, kuge::Transform2D{{x, y}});
            world.add<kuge::Collider>(e, kuge::Collider::box(w, h));
        };

        wall(ARENA_W / 2, -10.0f, ARENA_W + 40.0f, 20.0f);              // top
        wall(ARENA_W / 2, ARENA_H + 10.0f, ARENA_W + 40.0f, 20.0f);     // bottom
        wall(-10.0f, ARENA_H / 2, 20.0f, ARENA_H + 40.0f);              // left
        wall(ARENA_W + 10.0f, ARENA_H / 2, 20.0f, ARENA_H + 40.0f);     // right
    }

    // What a client simulates for its own ship: the arena and the ship, nothing else
    inline kw::Entity buildShip(kw::World& world, kuge::Vec2 at = {40.0f, ARENA_H / 2})
    {
        const kw::Entity ship = world.create();
        kuge::Body body;

        body.type = kuge::Body::Type::Kinematic;
        world.add<kuge::Transform2D>(ship, kuge::Transform2D{at});
        world.add<kuge::Body>(ship, body);
        world.add<kuge::Collider>(ship, kuge::Collider::box(SHIP_SIZE.x, SHIP_SIZE.y));
        return ship;
    }

    //! The movement part of an input: the room and the client's prediction both do this
    inline void steerShip(kw::World& world, kw::Entity ship, const Steer& steer)
    {
        world.get<kuge::Body>(ship).velocity = {steer.dx * SHIP_SPEED, steer.dy * SHIP_SPEED};
    }

    inline void stepArena(kw::World& world, double dt)
    {
        world.getResource<kuge::Physics2D>().step(world, static_cast<float>(dt));
    }

    // -- The rules (the room's) ---------------------------------------------------------------------------------
    //! A resource of the room's World
    struct Match
    {
        std::uint32_t                                                      tick = 0;
        std::uint32_t                                                      spawned = 0;
        std::uint32_t                                                      killed = 0;
        std::mt19937                                                       rng{1};
        //! Called for each entity the rules make, to have it replicated (bullets, enemies)
        std::function<void(kw::Entity, kuge::replication::EntityType, std::uint32_t)> track;
    };

    enum class Outcome { Playing, Won, Lost };

    //! Fires, if the gun is ready
    void fire(kw::World& world, kw::Entity ship);

    //! One tick of the rules: bullets fly, enemies come and move, things are hit, ships die
    void stepRules(kw::World& world, double dt);

    //! The game is over when every ship is dead (Lost), or the whole wave was spawned and is dead (Won)
    Outcome outcome(kw::World& world, bool anyShipEver);

    //! The ships of the World, by increasing entity
    std::vector<kw::Entity> ships(kw::World& world);

}
