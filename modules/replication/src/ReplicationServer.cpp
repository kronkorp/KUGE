#include "ReplicationServer.hpp"

namespace kuge::replication
{

    ReplicationServer::ReplicationServer(kw::World& world, const ReplicationRegistry& registry, net::Endpoint& endpoint, ReplicationServerConfig config)
        : m_world(world), m_registry(registry), m_endpoint(endpoint), m_config(config)
    {
        // (The endpoint may outlive this: its handler checks that this is still there)
        m_endpoint.on<SnapshotAck>([this, alive = m_alive](net::ConnectionId from, const SnapshotAck& ack) {
            if (*alive) {
                onAck(from, ack.tick);
            }
        });
    }

    ReplicationServer::~ReplicationServer(void)
    {
        *m_alive = false;
    }

    NetworkId ReplicationServer::track(kw::Entity entity, EntityType type, NetworkId owner)
    {
        const NetworkId id = m_nextId++;

        if (m_world.has<Replicated>(entity)) {
            m_world.get<Replicated>(entity) = Replicated{id, type, owner};
        } else {
            m_world.add<Replicated>(entity, Replicated{id, type, owner});
        }
        return id;
    }

    void ReplicationServer::untrack(kw::Entity entity)
    {
        if (m_world.has<Replicated>(entity)) {
            m_world.remove<Replicated>(entity);
        }
    }

    std::size_t ReplicationServer::tracked(void)
    {
        std::size_t count = 0;
        auto view = m_world.view<Replicated>();

        for ([[maybe_unused]] kw::Entity entity : view) {
            ++count;
        }
        return count;
    }

    void ReplicationServer::addClient(net::ConnectionId connection)
    {
        m_clients.emplace(connection, ClientState{});
    }

    void ReplicationServer::removeClient(net::ConnectionId connection)
    {
        m_clients.erase(connection);
    }

    void ReplicationServer::setInputAck(net::ConnectionId connection, std::uint32_t sequence)
    {
        const auto found = m_clients.find(connection);

        if (found != m_clients.end()) {
            found->second.inputAck = sequence;
        }
    }

    // The client says which snapshot it has: the next one is computed against it
    void ReplicationServer::onAck(net::ConnectionId connection, std::uint32_t tick)
    {
        const auto found = m_clients.find(connection);

        if (found == m_clients.end()) {
            return;
        }
        ClientState& client = found->second;

        ++m_stats.acksReceived;
        if (tick == 0) {                       // "I have nothing": start again from nothing
            client.baseline.reset();
            client.baselineTick = 0;
            client.history.clear();
            return;
        }
        const auto sent = client.history.find(tick);

        if (sent == client.history.end() || tick <= client.baselineTick) {
            return;                            // (an old ack, or one for a snapshot that is not kept)
        }
        client.baseline = sent->second;
        client.baselineTick = tick;
        client.history.erase(client.history.begin(), std::next(sent));
    }

    void ReplicationServer::send(net::ConnectionId connection, ClientState& client, std::uint32_t tick, const std::shared_ptr<const WorldState>& view)
    {
        const std::vector<Bytes> ops = diffOps(m_registry, client.baseline.get(), *view);
        std::vector<Bytes> parts(1);
        Bytes sent;
        bool skipped = false;

        for (const Bytes& op : ops) {
            if (op.size() > m_config.maxPartBytes) {
                ++m_stats.oversized;
                skipped = true;
                continue;
            }
            if (!parts.back().empty() && parts.back().size() + op.size() > m_config.maxPartBytes) {
                parts.emplace_back();
            }
            parts.back().insert(parts.back().end(), op.begin(), op.end());
            sent.insert(sent.end(), op.begin(), op.end());
        }
        // What this client will have once it has applied the snapshot, and will acknowledge: the next
        // snapshots are built on it. If a record was left out, that is not `view`: an entity that the client
        // never got would be in the base, and the updates that follow would be for something it does not have.
        // (Each record is complete on its own, so what remains is a state that makes sense.)
        std::shared_ptr<const WorldState> recorded = view;

        if (skipped) {
            static const WorldState nothing;

            recorded = std::make_shared<const WorldState>(applyOps(m_registry, client.baseline ? *client.baseline : nothing, sent));
        }
        for (std::size_t i = 0; i < parts.size(); ++i) {
            SnapshotPacket packet;

            packet.schema = m_registry.schema();
            packet.tick = tick;
            packet.baseTick = client.baselineTick;
            packet.inputAck = client.inputAck;
            packet.part = static_cast<std::uint16_t>(i);
            packet.parts = static_cast<std::uint16_t>(parts.size());
            packet.ops = std::move(parts[i]);
            m_stats.bytesSent += packet.ops.size();
            ++m_stats.packetsSent;
            m_endpoint.send(connection, packet, net::Channel::Unreliable);
        }
        ++m_stats.snapshotsSent;
        m_stats.fullSnapshots += client.baseline ? 0 : 1;
        client.history[tick] = recorded;
        client.lastSent = tick;
        client.sentOnce = true;
        while (client.history.size() > m_config.maxHistory) {
            client.history.erase(client.history.begin());
        }
    }

    void ReplicationServer::update(std::uint32_t tick)
    {
        std::vector<net::ConnectionId> due;

        for (const auto& [connection, client] : m_clients) {
            if (!client.sentOnce || tick - client.lastSent >= m_config.sendInterval) {
                due.push_back(connection);
            }
        }
        if (due.empty()) {
            return;
        }
        const auto current = std::make_shared<const WorldState>(captureState(m_world, m_registry));

        for (const net::ConnectionId connection : due) {
            ClientState& client = m_clients[connection];

            if (!m_filter) {
                send(connection, client, tick, current);
                continue;
            }
            auto seen = std::make_shared<WorldState>();

            for (const auto& [id, record] : *current) {
                if (m_filter(connection, id, record)) {
                    seen->emplace(id, record);
                }
            }
            send(connection, client, tick, seen);
        }
    }

}
