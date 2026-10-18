#pragma once
// Arena, the small 3D game: players run, jump and push crates in a walled room to pick up coins, up to four.
//
// This directory is what the server and the client share: the vocabulary (types, messages, what is
// replicated), the level, and the rules. The server (rooms) and the client (a window) are separate programs
// that include it, and a third one runs both in a single process. None of them changes the engine: this
// is the check that the 3D modules (render, physics, replication, prediction) make a game.

#include "CommonComponents.hpp"
#include "Physics3D.hpp"
#include "ReplicationServer.hpp"
#include "Transform3D.hpp"
#include "Wire.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace arena
{

    constexpr double TICK_DT = 1.0 / 60.0;
    constexpr float HALF_SIZE = 14.0f;         // meters: the floor goes from -14 to 14 in x and z
    constexpr float WALK_SPEED = 6.0f;         // meters per second
    constexpr float JUMP_SPEED = 6.5f;
    constexpr float PLAYER_RADIUS = 0.4f;
    constexpr float PLAYER_HEIGHT = 1.8f;
    constexpr std::size_t COIN_COUNT = 12;     // in a ring around the middle, and one ahead of where the players start

    // What kinds of entities the server tells the clients about
    constexpr kuge::replication::EntityType PLAYER = 1;
    constexpr kuge::replication::EntityType COIN = 2;
    constexpr kuge::replication::EntityType CRATE = 3;

    // -- What a player does: sent by the client, applied by the room, predicted by the client -------

    struct Steer
    {
        KUGE_MESSAGE(Steer, forward, strafe, jump, yaw)
        std::int8_t  forward = 0;     //!< 1: ahead, -1: back
        std::int8_t  strafe = 0;      //!< 1: to the right
        bool         jump = false;
        std::int16_t yaw = 0;         //!< Where the player looks, in thousandths of a radian (it turns the walk)
        bool operator==(const Steer&) const = default;
    };

    inline std::int16_t quantizeYaw(float radians)
    {
        const float turn = std::remainder(radians, 2.0f * 3.14159265f);   // (-pi, pi]

        return static_cast<std::int16_t>(std::lround(turn * 1000.0f));
    }

    // -- Components ---------------------------------------------------------------------------------

    struct Score { int points = 0; };                          //!< Replicated when it changes

    // The server alone knows these
    struct Owned { std::uint32_t player = 0; };                //!< The network id of the player a body belongs to
    struct Coin {};

    // -- What is replicated: the same list for the room and the clients -------------------------------

    inline kuge::replication::ReplicationRegistry makeRegistry(void)
    {
        using namespace kuge::replication;
        ReplicationRegistry registry;

        registerTransform3D(registry, Replicate::Interpolated, true);     // the player predicts its body
        registerBody3D(registry, Replicate::OnChange, true);
        registry.component<Score>("Score", Replicate::OnChange,
            [](kuge::ByteWriter& out, const Score& s) { out.write<std::int32_t>(s.points); },
            [](kuge::ByteReader& in) { return Score{in.read<std::int32_t>()}; });
        return registry;
    }

    // -- The level, and the player ------------------------------------------------------------------------

    //! The floor and the walls: what a client simulates for its own body, with the body (no crates, no coins)
    void buildLevel(kw::World& world);

    //! A player's body: a capsule that stays upright
    kw::Entity buildPlayer(kw::World& world, k3::Math::Vector3f at = {0.0f, 1.0f, 6.0f});

    //! The movement part of an input: the room and the client's prediction both do this
    void steerPlayer(kw::World& world, kw::Entity player, const Steer& steer);

    inline void stepWorld(kw::World& world, double dt)
    {
        world.getResource<kuge::Physics3D>().step(world, static_cast<float>(dt));
    }

    // -- The rules (the room's) ---------------------------------------------------------------------------------

    //! A resource of the room's World
    struct Match
    {
        //! Called for each entity the rules make, to have it replicated
        std::function<void(kw::Entity, kuge::replication::EntityType, std::uint32_t)> track;
    };

    enum class Outcome { Playing, Won };

    //! The coins and the crates
    void buildGame(kw::World& world);

    //! After the physics: whoever touched a coin takes it
    void collectCoins(kw::World& world);

    //! Won when every coin is taken
    Outcome outcome(kw::World& world);

    //! The body of a player (by network id), if it exists. Found by its owner each time, never kept: the World
    //! gives the number of a removed entity to the next one it makes.
    std::optional<kw::Entity> playerOf(kw::World& world, std::uint32_t player);

}
