#pragma once

#include "Replication.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace kuge::replication
{

    //! Names a thing that a player's input made: the same on the client that predicted it and on the room that made it for real.
    //! The player and the number of the input (`Prediction::sequence()` on the client, `InputServer::Applied::sequence` in the room),
    //! and which of the things that input made (0, 1...).
    struct PredictionKey
    {
        NetworkId     player = 0;
        std::uint32_t sequence = 0;
        std::uint8_t  index = 0;

        bool operator==(const PredictionKey&) const = default;
    };

    //! Registers PredictionKey (sent once, when the client learns of the entity): the room puts it on the real entity
    std::size_t registerPredictionKey(ReplicationRegistry& registry);

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Cosmetic things that the client makes at once, and that the room's real ones take the place of
     *
     * A click that throws a smoke, a grenade, a tracer: the client makes the entity at the click (it is not replicated: only the
     * client has it), the room makes the real one when the input arrives, with the same key. When the real one appears in the
     * client's World the predicted one is handed over to it, and one that the room never confirms (it refused the action) goes
     * after a delay.
     *
     *     PredictedSpawns spawns(world);
     *     const kw::Entity smoke = world.create();
     *     ...                                                                 // sprite, velocity: what it looks like
     *     spawns.add(smoke, {localPlayer, prediction.sequence(), 0});         // at the click
     *     spawns.update(dt);                                                  // each frame
     *
     *     // in the room, when it applies the same input:
     *     world.add<PredictionKey>(real, PredictionKey{player.networkId, applied.sequence, 0});
     *
     * By default the predicted entity is removed when the real one arrives; `onConfirmed` lets the game do better (move the real one's
     * sprite onto it so that nothing jumps). What the action does, and what it costs (the weapon, the ammo, the cooldown), is the
     * game's: the engine only matches two entities that carry the same key.
     */
    ////////////////////////////////////////////////////////////////////////////
    class PredictedSpawns
    {
        public:
            struct Config
            {
                double timeout = 1.0;   //!< Seconds after which a predicted entity that was not confirmed is removed
            };

            //! Called when the real entity is there: (predicted, real). Default: removes the predicted one.
            using Confirmed = std::function<void(kw::Entity predicted, kw::Entity real)>;
            //! Called before a predicted entity that the room never confirmed is removed. Default: nothing more.
            using Refused = std::function<void(kw::Entity predicted)>;

            explicit PredictedSpawns(kw::World& world) : PredictedSpawns(world, Config()) {}
            PredictedSpawns(kw::World& world, Config config) : m_world(world), m_config(config) {}

            void onConfirmed(Confirmed hook) { m_confirmed = std::move(hook); }
            void onRefused(Refused hook) { m_refused = std::move(hook); }

            //! A predicted entity; it gets the PredictionKey component
            void add(kw::Entity predicted, PredictionKey key);

            //! The game removed (or is going to remove) a predicted entity by itself: do not touch it any more
            void forget(kw::Entity predicted);

            //! Matches the pending ones with the real entities that came since, and removes the ones that timed out
            void update(double seconds);

            std::size_t pending(void) const noexcept { return m_pending.size(); }

        private:
            struct Pending
            {
                kw::Entity    entity;
                PredictionKey key;
                double        age = 0.0;
            };

            kw::World&           m_world;
            Config               m_config;
            std::vector<Pending> m_pending;
            Confirmed            m_confirmed;
            Refused              m_refused;
    };

}
