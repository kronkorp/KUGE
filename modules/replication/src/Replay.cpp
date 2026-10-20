#include "Replay.hpp"
#include <algorithm>
#include <cmath>

namespace kuge::replication
{

    namespace
    {
        constexpr std::uint32_t MAGIC = 0x50524B47;   // "GKRP"
        constexpr std::uint16_t VERSION = 1;
        constexpr std::uint8_t  KEYFRAME = 0, SNAPSHOT = 1, EVENT = 2;
    }

    // -- Recorder -------------------------------------------------------------------------------------
    Recorder::Recorder(const ReplicationRegistry& registry, RecorderConfig config) : m_registry(registry), m_config(std::move(config)) {}

    void Recorder::attach(ReplicationClient& client)
    {
        client.setTap([this](std::uint32_t tick, std::uint32_t, const WorldState& state) { record(tick, state); });
    }

    void Recorder::record(std::uint32_t tick, const WorldState& state)
    {
        const auto concat = [](const std::vector<Bytes>& ops) {
            Bytes all;

            for (const Bytes& op : ops) {
                all.insert(all.end(), op.begin(), op.end());
            }
            return all;
        };
        const auto keyframeTicks = static_cast<std::uint32_t>(std::max(1.0, m_config.keyframeSeconds * m_config.tickRate));
        const bool keyframe = !m_any || tick - m_lastKeyframe >= keyframeTicks;

        if (m_any && tick <= m_lastTick) {
            return;   // old, or again
        }
        if (keyframe) {
            m_segments.emplace_back();
            m_segments.back().push_back({KEYFRAME, tick, 0, concat(diffOps(m_registry, nullptr, state))});
            m_lastKeyframe = tick;
        } else {
            m_segments.back().push_back({SNAPSHOT, tick, 0, concat(diffOps(m_registry, &m_previous, state))});
        }
        m_previous = state;
        m_lastTick = tick;
        m_any = true;
        // A ring: the oldest part goes, a whole segment at a time, while what remains is still long enough
        if (m_config.ringSeconds > 0.0) {
            const auto keep = static_cast<std::uint32_t>(m_config.ringSeconds * m_config.tickRate);

            while (m_segments.size() > 1 && tick - m_segments[1].front().tick >= keep) {
                m_segments.pop_front();
            }
        }
    }

    void Recorder::event(std::uint32_t tick, std::uint16_t type, std::span<const std::uint8_t> bytes)
    {
        if (m_segments.empty()) {
            return;   // nothing to attach it to yet
        }
        m_segments.back().push_back({EVENT, tick, type, Bytes(bytes.begin(), bytes.end())});
    }

    std::size_t Recorder::entries(void) const noexcept
    {
        std::size_t count = 0;

        for (const auto& segment : m_segments) {
            count += segment.size();
        }
        return count;
    }

    std::vector<std::uint8_t> Recorder::bytes(void) const
    {
        ByteWriter out;

        out.writeHeader(MAGIC, VERSION);
        out.write<std::uint32_t>(m_registry.schema());
        out.write<std::uint16_t>(m_config.protocol);
        out.write<std::uint32_t>(m_config.tickRate);
        out.writeString(m_config.info);
        out.write<std::uint32_t>(static_cast<std::uint32_t>(entries()));
        for (const auto& segment : m_segments) {
            for (const Entry& entry : segment) {
                out.write<std::uint8_t>(entry.kind);
                out.write<std::uint32_t>(entry.tick);
                out.write<std::uint16_t>(entry.type);
                out.write<std::uint32_t>(static_cast<std::uint32_t>(entry.data.size()));
                out.writeBytes(entry.data);
            }
        }
        return out.bytes();
    }

    void Recorder::save(const std::filesystem::path& file) const
    {
        try {
            writeFile(file, bytes());
        } catch (const std::exception& error) {
            throw ReplayError(std::string("cannot write the replay: ") + error.what());
        }
    }

    // -- Replayer -------------------------------------------------------------------------------------
    Replayer::Replayer(const ReplicationRegistry& registry, std::vector<std::uint8_t> file, ReplicationClient& client, Config config)
        : m_registry(registry), m_client(client)
    {
        try {
            ByteReader in(file);

            in.readHeader(MAGIC);
            m_schema = in.read<std::uint32_t>();
            const auto protocol = in.read<std::uint16_t>();

            m_tickRate = in.read<std::uint32_t>();
            m_info = in.readString();
            if (m_schema != registry.schema()) {
                throw ReplayError("this replay was recorded with other components than this game has (an older or newer version)");
            }
            if (protocol != config.protocol) {
                throw ReplayError("this replay was recorded with another protocol (" + std::to_string(protocol) + ", this game has " + std::to_string(config.protocol) + ")");
            }
            const auto count = in.read<std::uint32_t>();

            for (std::uint32_t i = 0; i < count; ++i) {
                Entry entry;

                entry.kind = in.read<std::uint8_t>();
                entry.tick = in.read<std::uint32_t>();
                entry.type = in.read<std::uint16_t>();
                const auto size = in.read<std::uint32_t>();
                const auto data = in.readBytes(size);

                entry.data.assign(data.begin(), data.end());
                if (entry.kind != KEYFRAME && entry.kind != SNAPSHOT && entry.kind != EVENT) {
                    throw ReplayError("the file is damaged");
                }
                m_entries.push_back(std::move(entry));
            }
            if (!in.atEnd()) {
                throw ReplayError("the file is damaged");
            }
        } catch (const SerializerError&) {
            throw ReplayError("this is not a replay, or it is damaged");
        }
        if (m_entries.empty() || m_entries.front().kind != KEYFRAME) {
            throw ReplayError("this replay has no keyframe to start from");
        }
        for (const Entry& entry : m_entries) {
            if (entry.kind != EVENT) {
                m_last = entry.tick;
            }
        }
        m_first = m_entries.front().tick;
        m_tick = m_first;
    }

    Replayer Replayer::load(const ReplicationRegistry& registry, const std::filesystem::path& file, ReplicationClient& client, Config config)
    {
        try {
            return Replayer(registry, readFile(file), client, config);
        } catch (const SerializerError& error) {
            throw ReplayError(std::string("cannot read the replay: ") + error.what());
        }
    }

    void Replayer::feed(const Entry& entry)
    {
        if (entry.kind == EVENT) {
            if (m_event && !m_quiet) {
                m_event(entry.tick, entry.type, entry.data);
            }
            return;
        }
        SnapshotPacket packet;

        packet.schema = m_schema;
        packet.tick = entry.tick;
        packet.baseTick = entry.kind == KEYFRAME ? 0 : m_previousTick;
        packet.ops = entry.data;
        m_client.receive(packet);
        m_previousTick = entry.tick;
    }

    void Replayer::feedUpTo(double tick)
    {
        while (m_next < m_entries.size() && static_cast<double>(m_entries[m_next].tick) <= tick) {
            feed(m_entries[m_next++]);
        }
    }

    void Replayer::update(double frameSeconds)
    {
        const double seconds = m_paused ? 0.0 : frameSeconds * m_speed;

        m_tick = std::min(m_tick + seconds * m_tickRate, static_cast<double>(m_last));
        feedUpTo(m_tick);
        m_client.update(seconds);
    }

    void Replayer::seek(double tick)
    {
        tick = std::clamp(tick, static_cast<double>(m_first), static_cast<double>(m_last));
        // The keyframe at or before it
        std::size_t key = 0;

        for (std::size_t i = 0; i < m_entries.size() && static_cast<double>(m_entries[i].tick) <= tick; ++i) {
            if (m_entries[i].kind == KEYFRAME) {
                key = i;
            }
        }
        m_client.reset();
        m_previousTick = 0;
        m_next = key;
        m_tick = tick;
        m_quiet = true;
        feedUpTo(tick);
        m_quiet = false;
    }

}
