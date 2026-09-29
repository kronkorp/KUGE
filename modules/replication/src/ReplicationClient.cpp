#include "ReplicationClient.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <cmath>

namespace kuge::replication
{

    ReplicationClient::ReplicationClient(kw::World& world, const ReplicationRegistry& registry, ReplicationClientConfig config)
        : m_world(world), m_registry(registry), m_config(config)
    {
    }

    ReplicationClient::~ReplicationClient(void)
    {
        *m_alive = false;
    }

    void ReplicationClient::attach(net::Endpoint& room)
    {
        m_room = &room;
        room.on<SnapshotPacket>([this, alive = m_alive](net::ConnectionId, const SnapshotPacket& packet) {
            if (*alive) {
                onPacket(packet);
            }
        });
    }

    std::optional<kw::Entity> ReplicationClient::entity(NetworkId id) const
    {
        const auto found = m_entities.find(id);

        if (found == m_entities.end()) {
            return std::nullopt;
        }
        return found->second.entity;
    }

    void ReplicationClient::ack(std::uint32_t tick)
    {
        if (m_room && m_room->connected()) {
            m_room->send(m_room->connections().front(), SnapshotAck{tick}, net::Channel::Unreliable);
        }
    }

    // -- Receiving --------------------------------------------------------------------------------------
    void ReplicationClient::onPacket(const SnapshotPacket& packet)
    {
        ++m_stats.packetsReceived;
        if (packet.schema != m_registry.schema()) {
            ++m_stats.wrongSchema;
            return;
        }
        if (packet.tick <= m_lastApplied || packet.parts == 0 || packet.part >= packet.parts) {
            ++m_stats.ignored;
            return;
        }
        Pending& pending = m_pending[packet.tick];

        if (pending.parts.empty()) {
            pending.parts.resize(packet.parts);
            pending.baseTick = packet.baseTick;
            pending.inputAck = packet.inputAck;
        }
        if (pending.parts.size() != packet.parts || pending.baseTick != packet.baseTick || pending.parts[packet.part]) {
            ++m_stats.ignored;   // (a copy, or nonsense)
            return;
        }
        pending.parts[packet.part] = packet.ops;
        ++pending.have;
        if (pending.have < pending.parts.size()) {
            while (m_pending.size() > m_config.maxPending) {
                m_pending.erase(m_pending.begin());
            }
            return;
        }
        // All the parts are here
        Bytes ops;

        for (const auto& part : pending.parts) {
            ops.insert(ops.end(), part->begin(), part->end());
        }
        const std::uint32_t tick = packet.tick, base = pending.baseTick, inputAck = pending.inputAck;

        m_pending.erase(m_pending.begin(), m_pending.upper_bound(tick));   // this one, and the older ones that will not come
        process(tick, base, inputAck, ops);
    }

    void ReplicationClient::process(std::uint32_t tick, std::uint32_t baseTick, std::uint32_t inputAck, const Bytes& ops)
    {
        static const WorldState nothing;
        const WorldState* baseline = &nothing;

        if (baseTick != 0) {
            const auto found = m_states.find(baseTick);

            if (found == m_states.end()) {
                ++m_stats.missingBaseline;
                ack(0);   // "I cannot follow: send everything"
                return;
            }
            baseline = &found->second;
        }
        WorldState next;

        try {
            next = applyOps(m_registry, *baseline, ops);
        } catch (const SerializerError& error) {
            ++m_stats.malformed;
            Logger::logger().warn("replication: a snapshot was dropped: {}", error.what());
            return;
        }
        applyToWorld(next, tick, inputAck);
        m_states[tick] = std::move(next);
        while (m_states.size() > m_config.maxStates) {
            m_states.erase(m_states.begin());
        }
        m_lastApplied = tick;
        m_inputAck = inputAck;
        ++m_stats.snapshotsApplied;
        m_sinceLatest = 0.0;
        if (!m_started) {
            m_started = true;
            m_renderTick = tick - m_config.interpolationDelay * m_config.tickRate;
        }
        ack(tick);
    }

    // -- Making the World look like the state ------------------------------------------------------------
    void ReplicationClient::spawn(NetworkId id, const EntityRecord& record, std::uint32_t tick)
    {
        const kw::Entity entity = m_world.create();
        Tracked tracked;

        tracked.entity = entity;
        m_world.add<Replicated>(entity, Replicated{id, record.type, record.owner});
        for (const auto& [index, bytes] : record.components) {
            const auto& entry = m_registry.entry(index);

            try {
                entry.apply(m_world, entity, bytes);   // (the first value; a prediction takes over afterwards)
            } catch (const SerializerError&) {
                ++m_stats.malformed;
            }
            if (entry.mode == Replicate::Interpolated && !(isOwned(record) && entry.predicted)) {
                tracked.samples[index].push_back(Sample{tick, bytes});
            }
        }
        m_entities[id] = std::move(tracked);
        ++m_stats.spawned;
        const auto hook = m_spawn.find(record.type);

        if (hook != m_spawn.end()) {
            hook->second(m_world, entity, SpawnInfo{id, record.type, record.owner});
        }
    }

    void ReplicationClient::applyToWorld(const WorldState& next, std::uint32_t tick, std::uint32_t inputAck)
    {
        // Gone
        for (auto it = m_current.begin(); it != m_current.end();) {
            if (next.count(it->first)) {
                ++it;
                continue;
            }
            const auto tracked = m_entities.find(it->first);

            if (tracked != m_entities.end()) {
                if (m_ownedGone && isOwned(it->second)) {
                    m_ownedGone(tracked->second.entity);
                }
                const auto hook = m_destroy.find(it->second.type);

                if (hook != m_destroy.end()) {
                    hook->second(m_world, tracked->second.entity);
                }
                m_world.remove(tracked->second.entity);
                m_entities.erase(tracked);
                ++m_stats.destroyed;
            }
            it = m_current.erase(it);
        }
        for (const auto& [id, record] : next) {
            const auto before = m_current.find(id);

            if (before == m_current.end() || m_entities.find(id) == m_entities.end()) {
                spawn(id, record, tick);
            } else {
                Tracked& tracked = m_entities[id];

                for (const auto& [index, bytes] : record.components) {
                    const auto& entry = m_registry.entry(index);
                    const auto old = before->second.components.find(index);
                    const bool changed = old == before->second.components.end() || old->second != bytes;

                    if (isOwned(record) && entry.predicted) {
                        continue;
                    }
                    if (entry.mode == Replicate::Interpolated) {
                        tracked.samples[index].push_back(Sample{tick, bytes});   // (also when it did not change: it stayed there until this tick)
                    } else if (changed && entry.mode == Replicate::OnChange) {
                        try {
                            entry.apply(m_world, tracked.entity, bytes);
                        } catch (const SerializerError&) {
                            ++m_stats.malformed;
                        }
                    }
                }
                for (const auto& [index, bytes] : before->second.components) {
                    if (!record.components.count(index) && m_registry.entry(index).mode != Replicate::OnSpawn) {
                        m_registry.entry(index).remove(m_world, tracked.entity);
                        tracked.samples.erase(index);
                    }
                }
            }
            if (isOwned(record) && m_owned) {
                m_owned(m_entities[id].entity, record, tick, inputAck);
            }
        }
        m_current = next;
    }

    // -- Interpolation ----------------------------------------------------------------------------------
    void ReplicationClient::update(double frameSeconds)
    {
        if (!m_started) {
            return;
        }
        const double rate = m_config.tickRate;
        const double delay = m_config.interpolationDelay * rate;

        m_sinceLatest += frameSeconds;
        // Drawn `delay` behind the newest snapshot, moving on with the clock, and easing towards where that should be
        const double target = m_lastApplied - delay + m_sinceLatest * rate;

        m_renderTick += frameSeconds * rate;
        m_renderTick += (target - m_renderTick) * std::min(1.0, frameSeconds * 5.0);
        m_renderTick = std::min(m_renderTick, static_cast<double>(m_lastApplied));

        for (auto& [id, tracked] : m_entities) {
            for (auto& [index, samples] : tracked.samples) {
                const auto& entry = m_registry.entry(index);

                while (samples.size() >= 2 && samples[1].tick <= m_renderTick) {
                    samples.pop_front();
                }
                try {
                    if (samples.size() >= 2 && entry.interpolate) {
                        const double span = static_cast<double>(samples[1].tick) - samples[0].tick;
                        const double t = std::clamp((m_renderTick - samples[0].tick) / span, 0.0, 1.0);

                        entry.interpolate(m_world, tracked.entity, samples[0].bytes, samples[1].bytes, static_cast<float>(t));
                    } else if (!samples.empty()) {
                        entry.apply(m_world, tracked.entity, samples.back().bytes);   // one value: it waits there
                    }
                } catch (const SerializerError&) {
                    ++m_stats.malformed;
                }
            }
        }
    }

}
