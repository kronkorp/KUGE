#pragma once

#include "Endpoint.hpp"
#include "ReplicationProtocol.hpp"
#include "WorldState.hpp"
#include <functional>
#include <map>
#include <memory>

namespace kuge::replication
{

    struct ReplicationServerConfig
    {
        std::uint32_t sendInterval = 2;      //!< A snapshot every this many ticks (2 at 60 Hz: 30 snapshots per second)
        std::size_t   maxHistory   = 64;     //!< Snapshots kept for a client that has not acknowledged them yet
        std::size_t   maxPartBytes = 6000;   //!< A snapshot that is bigger goes in several packets
    };

    struct ReplicationServerStats
    {
        std::uint64_t snapshotsSent = 0;
        std::uint64_t packetsSent = 0;
        std::uint64_t bytesSent = 0;         //!< Of the changes (without the headers)
        std::uint64_t fullSnapshots = 0;     //!< Sent from nothing (a new client, or one that asked)
        std::uint64_t acksReceived = 0;
        std::uint64_t oversized = 0;         //!< Records too big for a packet: not sent
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What a room does to show its world to its players: it says which
     *         entities exist, and sends each client what changed since the
     *         last snapshot that client acknowledged
     *
     *     ReplicationServer replication(world(), registry, endpoint());
     *
     *     const NetworkId id = replication.track(ship, ShipType, player.networkId);
     *     replication.addClient(player.connection);                 // in onPlayerJoined
     *     replication.removeClient(player.connection);              // in onPlayerLeft
     *     replication.update(time.tick);                            // each tick, in the Replication stage
     *
     * Snapshots go on the unreliable channel. A client acknowledges what it applied, and the server
     * computes the next one against that: a lost snapshot costs nothing more than a bigger next one, and a
     * client that missed everything is simply sent everything. An entity that is not tracked any more (or
     * whose entity was removed from the World) disappears from the clients at the next snapshot.
     * Entities and components are seen in order of their ids, so the same world gives the same bytes.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ReplicationServer
    {
        public:
            using Filter = std::function<bool(net::ConnectionId, NetworkId, const EntityRecord&)>;

            ReplicationServer(kw::World& world, const ReplicationRegistry& registry, net::Endpoint& endpoint, ReplicationServerConfig config = {});
            ~ReplicationServer(void);

            ReplicationServer(const ReplicationServer&)            = delete;
            ReplicationServer& operator=(const ReplicationServer&) = delete;

            //! Makes an entity replicated. Its components (the registered ones) are sent as they are.
            //! @param owner  The player it belongs to (the network id of the room's Player), 0 for nobody
            //! @return       Its network id (never used again)
            NetworkId track(kw::Entity entity, EntityType type, NetworkId owner = 0);

            //! Stops replicating an entity (the clients remove it). It stays in the World.
            void untrack(kw::Entity entity);

            void addClient(net::ConnectionId connection);
            void removeClient(net::ConnectionId connection);
            bool hasClient(net::ConnectionId connection) const { return m_clients.count(connection) != 0; }

            //! The last input of a client that was applied (it goes back to it in each snapshot: see Prediction)
            void setInputAck(net::ConnectionId connection, std::uint32_t sequence);

            //! What a client may see (interest management): everything if not set
            void setFilter(Filter filter) { m_filter = std::move(filter); }

            //! Once per tick, with the number of the tick: sends what is due
            void update(std::uint32_t tick);

            const ReplicationServerStats& stats(void) const noexcept { return m_stats; }
            std::size_t                   tracked(void);

        private:
            struct ClientState
            {
                std::shared_ptr<const WorldState>                      baseline;       //!< What it acknowledged
                std::uint32_t                                          baselineTick = 0;
                std::map<std::uint32_t, std::shared_ptr<const WorldState>> history;   //!< What was sent, not acknowledged yet
                std::uint32_t                                          lastSent = 0;
                bool                                                   sentOnce = false;
                std::uint32_t                                          inputAck = 0;
            };

            void onAck(net::ConnectionId connection, std::uint32_t tick);
            void send(net::ConnectionId connection, ClientState& client, std::uint32_t tick, const std::shared_ptr<const WorldState>& view);

            kw::World&                                 m_world;
            const ReplicationRegistry&                 m_registry;
            net::Endpoint&                             m_endpoint;
            ReplicationServerConfig                    m_config;
            NetworkId                                  m_nextId = 1;
            std::map<net::ConnectionId, ClientState>   m_clients;
            Filter                                     m_filter;
            ReplicationServerStats                     m_stats;
            std::shared_ptr<bool>                      m_alive = std::make_shared<bool>(true);
    };

}
