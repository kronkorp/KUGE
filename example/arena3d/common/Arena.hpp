#pragma once
// Arena, the small 3D game: up to four players run, jump, push crates and shoot one another in a walled arena with
// platforms, ramps and cover. The first to take FRAG_LIMIT frags wins.
//
// This directory is what the server and the client share: the vocabulary (types, messages, what is replicated),
// the map, and the rules. The server (rooms) and the client (a window) are separate programs that include it, and
// a third one runs both in a single process. None of them changes the engine: this is the check that the 3D
// modules (render, physics, replication, prediction, animation) make a game.

#include "AnimationState3D.hpp"
#include "CommonComponents.hpp"
#include "Map.hpp"
#include "Physics3D.hpp"
#include "ReplicationServer.hpp"
#include "Transform3D.hpp"
#include "Wire.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace arena
{

    constexpr double TICK_DT = 1.0 / 60.0;
    constexpr float WALK_SPEED = 6.0f;         // meters per second
    constexpr float JUMP_SPEED = 6.5f;
    constexpr float PLAYER_RADIUS = 0.4f;
    constexpr float PLAYER_HEIGHT = 1.8f;      // the capsule: the entity is its middle, 0.9 m over the feet
    constexpr float EYE_HEIGHT = 0.6f;         // over the middle of the capsule
    constexpr int   HEALTH = 100;
    constexpr int   DAMAGE = 25;               // four shots kill
    constexpr float BULLET_SPEED = 45.0f;      // meters per second
    constexpr int   BULLET_LIFE = 120;         // ticks
    constexpr int   FIRE_COOLDOWN = 12;        // ticks between two shots
    constexpr int   RESPAWN_DELAY = 150;       // ticks
    constexpr int   CORPSE_LIFE = 150;         // ticks a dead body stays
    constexpr int   FRAG_LIMIT = 8;

    // What kinds of entities the server tells the clients about
    constexpr kuge::replication::EntityType PLAYER = 1;
    constexpr kuge::replication::EntityType CRATE = 2;
    constexpr kuge::replication::EntityType BULLET = 3;
    constexpr kuge::replication::EntityType CORPSE = 4;

    // -- What a player does: sent by the client, applied by the room, predicted by the client -------

    struct Steer
    {
        KUGE_MESSAGE(Steer, forward, strafe, jump, fire, yaw, pitch)
        std::int8_t  forward = 0;     //!< 1: ahead, -1: back
        std::int8_t  strafe = 0;      //!< 1: to the right
        bool         jump = false;
        bool         fire = false;
        std::int16_t yaw = 0;         //!< Where the player looks, in thousandths of a radian (it turns the walk and the body)
        std::int16_t pitch = 0;       //!< Up is positive: where a shot goes
        bool operator==(const Steer&) const = default;
    };

    inline std::int16_t quantize(float radians)
    {
        return static_cast<std::int16_t>(std::lround(std::remainder(radians, 2.0f * 3.14159265f) * 1000.0f));   // (-pi, pi]
    }

    // -- Components ---------------------------------------------------------------------------------

    struct Health { int points = HEALTH; };                    //!< Replicated when it changes
    struct Score  { int frags = 0; };                          //!< Replicated when it changes

    // The server alone knows these
    struct Owned      { std::uint32_t player = 0; };           //!< The network id of the player a body belongs to
    struct Fighter    { int cooldown = 0; int flash = 0; int airborne = 0; };
    struct Projectile { k3::Math::Vector3f velocity; std::uint32_t owner = 0; int life = BULLET_LIFE; };
    struct Corpse     { int life = CORPSE_LIFE; };

    // -- What is replicated: the same list for the room and the clients -------------------------------

    inline kuge::replication::ReplicationRegistry makeRegistry(void)
    {
        using namespace kuge::replication;
        ReplicationRegistry registry;

        registerTransform3D(registry, Replicate::Interpolated, true);     // the player predicts its body
        registerBody3D(registry, Replicate::OnChange, true);
        registerAnimationState3D(registry);
        registry.component<Health>("Health", Replicate::OnChange,
            [](kuge::ByteWriter& out, const Health& h) { out.write<std::int32_t>(h.points); },
            [](kuge::ByteReader& in) { return Health{in.read<std::int32_t>()}; });
        registry.component<Score>("Score", Replicate::OnChange,
            [](kuge::ByteWriter& out, const Score& s) { out.write<std::int32_t>(s.frags); },
            [](kuge::ByteReader& in) { return Score{in.read<std::int32_t>()}; });
        return registry;
    }

    // -- The level, and the player ------------------------------------------------------------------------

    //! The map as colliders: what a client simulates for its own body, with the body (no crates)
    void buildLevel(kw::World& world);

    //! A player's body: a capsule that stays upright. `yaw` turns it.
    kw::Entity buildPlayer(kw::World& world, Vec3 at = {0.0f, 1.0f, 6.0f}, float yaw = 0.0f);

    //! The movement part of an input: the room and the client's prediction both do this (it also turns the body)
    void steerPlayer(kw::World& world, kw::Entity player, const Steer& steer);

    inline void stepWorld(kw::World& world, double dt)
    {
        world.getResource<kuge::Physics3D>().step(world, static_cast<float>(dt));
    }

    //! The way a shot goes (a unit vector), and how a body or a bullet faces to go that way
    Vec3 aim(float yaw, float pitch);
    k3::Math::Vector4f facing(float yaw, float pitch);

    //! What a client simulates besides its player, to be stopped by what stops it on the server, and to push what it
    //! pushes: the crates (bodies that are simulated there too, and put back where the server has them when they are
    //! too far from it), and the other players (bodies that stand where they are shown: not pushed). `shown` is the
    //! world the client draws, with the replicated entities.
    struct Proxies
    {
        std::map<std::uint32_t, kw::Entity>  bodies;       //!< By network id
        kw::World*                           of = nullptr; //!< The predicted world they are in (a new one: they are gone)
    };

    //! Once per tick, before the physics of the predicted world; `me` is the network id of the player
    void syncProxies(Proxies& proxies, kw::World& predicted, kw::World& shown, std::uint32_t me);

    // -- The rules (the room's) ---------------------------------------------------------------------------------

    //! A resource of the room's World
    struct Match
    {
        //! Called for each entity the rules make, to have it replicated
        std::function<void(kw::Entity, kuge::replication::EntityType, std::uint32_t)> track;

        std::map<std::uint32_t, int>  frags;      //!< By player (network id), kept through the deaths
        struct Respawn { std::uint32_t player; int ticks; };
        std::vector<Respawn>          respawns;
        std::uint32_t                 spawned = 0;   //!< Bodies made: it varies where the next appears
        std::uint32_t                 winner = 0;    //!< 0: nobody yet
    };

    //! A crate: a cube of 1 m that a player pushes (the room has them all; a client has the ones near its player,
    //! to predict what it pushes)
    kw::Entity buildCrate(kw::World& world, Vec3 at, k3::Math::Vector4f rotation = {0.0f, 0.0f, 0.0f, 1.0f});

    //! The crates
    void buildGame(kw::World& world);

    //! A player appears (joining, or after a death) where it is farthest from the others
    kw::Entity spawnPlayer(kw::World& world, std::uint32_t player);

    //! The player shoots, if its gun is ready
    void fire(kw::World& world, kw::Entity shooter, const Steer& steer);

    //! One tick of the rules, after the physics: bullets fly and hit, the gun cools, the dead wait and come back, the
    //! clip of each character follows what it does
    void stepRules(kw::World& world);

    //! Somebody won when it has FRAG_LIMIT frags
    bool finished(kw::World& world);

    //! The body of a player (by network id), if it is alive. Found by its owner each time, never kept: the World
    //! gives the number of a removed entity to the next one it makes.
    std::optional<kw::Entity> playerOf(kw::World& world, std::uint32_t player);

}
