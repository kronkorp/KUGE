#pragma once

#include "Endpoint.hpp"
#include "ReplicationProtocol.hpp"
#include "WorldState.hpp"
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace kuge::replication
{

    //! What a prefab is told about a new entity
    struct SpawnInfo
    {
        NetworkId  id;
        EntityType type;
        NetworkId  owner;
    };

    struct ReplicationClientConfig
    {
        std::uint32_t tickRate           = 60;     //!< Of the server (Welcome says it)
        double        interpolationDelay = 0.1;    //!< Seconds behind the server that other entities are drawn (a few snapshots)
        std::size_t   maxStates          = 32;    //!< Snapshots kept to build the next ones from
        std::size_t   maxPending         = 16;    //!< Snapshots that are waiting for their other parts
    };

    struct ReplicationClientStats
    {
        std::uint64_t snapshotsApplied = 0;
        std::uint64_t packetsReceived = 0;
        std::uint64_t ignored = 0;           //!< Old, or already applied
        std::uint64_t missingBaseline = 0;   //!< Built on a snapshot that this client does not have (it asks for everything)
        std::uint64_t malformed = 0;
        std::uint64_t wrongSchema = 0;       //!< From a server whose list of components is not ours
        std::uint64_t spawned = 0;
        std::uint64_t destroyed = 0;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The other side of ReplicationServer: keeps the entities of the
     *         server in a World, as they were told
     *
     *     ReplicationClient replication(world, registry);
     *
     *     replication.onSpawn(ShipType, [](kw::World& world, kw::Entity entity, const kuge::replication::SpawnInfo&) {
     *         world.add<kuge::Sprite>(entity, ...);             // what the server does not send: how it looks
     *     });
     *     replication.attach(room);                             // the Endpoint of the room (see MatchmakingClient)
     *     ...
     *     replication.update(frameSeconds);                     // each frame: places the interpolated components
     *
     * An entity that the server tracks appears in the World (with a Replicated, and its components), and goes
     * when the server stops sending it. Components sent OnChange are set when they change, and those sent
     * Interpolated are drawn a short time in the past (interpolationDelay), between the last two values that
     * the server sent, so that movement is smooth even though snapshots come 30 times a second (and some
     * are lost). Nothing is invented: past the last value, an entity waits.
     *
     * Each snapshot is built from an older one that this client acknowledged, so the client keeps the last
     * ones. A snapshot it cannot build is ignored, and it asks the server for everything.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ReplicationClient
    {
        public:
            using SpawnHook   = std::function<void(kw::World&, kw::Entity, const SpawnInfo&)>;
            using DestroyHook = std::function<void(kw::World&, kw::Entity)>;
            //! The server's word on the entity that belongs to this player: for Prediction
            using OwnedHook   = std::function<void(kw::Entity, const EntityRecord&, std::uint32_t tick, std::uint32_t inputAck)>;

            ReplicationClient(kw::World& world, const ReplicationRegistry& registry, ReplicationClientConfig config = {});
            ~ReplicationClient(void);

            ReplicationClient(const ReplicationClient&)            = delete;
            ReplicationClient& operator=(const ReplicationClient&) = delete;

            //! Listens to the snapshots of a room. Its acknowledgements go back on it.
            void attach(net::Endpoint& room);

            //! Called (once the components are there) when an entity of this type appears. Replaces the previous.
            void onSpawn(EntityType type, SpawnHook hook) { m_spawn[type] = std::move(hook); }

            //! Called before an entity of this type is removed
            void onDestroy(EntityType type, DestroyHook hook) { m_destroy[type] = std::move(hook); }

            //! The network id of this player (from Welcome): what it owns can be predicted (see predictType)
            void setLocalPlayer(NetworkId player) { m_localPlayer = player; }

            //! The entities of this type that belong to this player are the client's own: what their `predicted`
            //! components hold is set when they appear, then only given to onOwned(). Other entities of the player
            //! (its bullets, say) are ordinary ones, moved by the server.
            void predictType(EntityType type) { m_predictedTypes.insert(type); }
            void onOwned(OwnedHook hook) { m_owned = std::move(hook); }

            std::optional<kw::Entity> entity(NetworkId id) const;
            std::size_t               entityCount(void) const noexcept { return m_entities.size(); }

            //! Each frame: moves the time of the interpolation, and places the interpolated components
            void update(double frameSeconds);

            std::uint32_t                 lastTick(void) const noexcept { return m_lastApplied; }
            std::uint32_t                 inputAck(void) const noexcept { return m_inputAck; }
            double                        renderTick(void) const noexcept { return m_renderTick; }
            const ReplicationClientStats& stats(void) const noexcept { return m_stats; }

        private:
            struct Sample
            {
                std::uint32_t tick;
                Bytes         bytes;
            };

            struct Pending
            {
                std::uint32_t                   baseTick = 0;
                std::uint32_t                   inputAck = 0;
                std::vector<std::optional<Bytes>> parts;
                std::size_t                     have = 0;
            };

            struct Tracked
            {
                kw::Entity                                     entity{};
                std::map<std::uint8_t, std::deque<Sample>>     samples;   //!< Interpolated components
            };

            void onPacket(const SnapshotPacket& packet);
            void process(std::uint32_t tick, std::uint32_t baseTick, std::uint32_t inputAck, const Bytes& ops);
            void applyToWorld(const WorldState& next, std::uint32_t tick, std::uint32_t inputAck);
            void spawn(NetworkId id, const EntityRecord& record, std::uint32_t tick);
            bool isOwned(const EntityRecord& record) const { return m_localPlayer != 0 && record.owner == m_localPlayer && m_predictedTypes.count(record.type) != 0; }
            void ack(std::uint32_t tick);

            kw::World&                              m_world;
            const ReplicationRegistry&              m_registry;
            ReplicationClientConfig                 m_config;
            net::Endpoint*                          m_room = nullptr;
            std::map<EntityType, SpawnHook>         m_spawn;
            std::map<EntityType, DestroyHook>       m_destroy;
            NetworkId                               m_localPlayer = 0;
            std::set<EntityType>                    m_predictedTypes;
            OwnedHook                               m_owned;

            std::map<std::uint32_t, WorldState>     m_states;        //!< Applied snapshots, by tick
            WorldState                              m_current;       //!< What the World shows (the last applied)
            std::map<NetworkId, Tracked>            m_entities;
            std::map<std::uint32_t, Pending>        m_pending;
            std::uint32_t                           m_lastApplied = 0;
            std::uint32_t                           m_inputAck = 0;

            bool                                    m_started = false;
            double                                  m_renderTick = 0.0;
            double                                  m_sinceLatest = 0.0;
            ReplicationClientStats                  m_stats;
            std::shared_ptr<bool>                   m_alive = std::make_shared<bool>(true);
    };

}
