#include "WorldState.hpp"
#include <algorithm>

namespace kuge::replication
{

    namespace
    {
        enum Kind : std::uint8_t { Spawn = 1, Update = 2, Destroy = 3 };

        void writeComponents(ByteWriter& out, const std::map<std::uint8_t, Bytes>& components, std::uint64_t mask)
        {
            for (const auto& [index, bytes] : components) {
                if (mask & (std::uint64_t{1} << index)) {
                    out.write<std::uint16_t>(static_cast<std::uint16_t>(bytes.size()));
                    out.writeBytes(bytes);
                }
            }
        }
    }

    WorldState captureState(kw::World& world, const ReplicationRegistry& registry)
    {
        WorldState state;
        auto view = world.view<Replicated>();

        for (kw::Entity entity : view) {
            const auto& info = world.get<Replicated>(entity);
            EntityRecord record;

            record.type = info.type;
            record.owner = info.owner;
            for (std::size_t i = 0; i < registry.size(); ++i) {
                const auto& entry = registry.entry(i);

                if (entry.has(world, entity)) {
                    ByteWriter out;

                    entry.write(world, entity, out);
                    record.components[static_cast<std::uint8_t>(i)] = out.bytes();
                }
            }
            state[info.id] = std::move(record);
        }
        return state;
    }

    std::vector<Bytes> diffOps(const ReplicationRegistry& registry, const WorldState* baseline, const WorldState& current)
    {
        std::vector<Bytes> ops;

        for (const auto& [id, record] : current) {
            const auto before = baseline ? baseline->find(id) : WorldState::const_iterator();

            if (!baseline || before == baseline->end()) {
                std::uint64_t mask = 0;

                for (const auto& [index, bytes] : record.components) {
                    mask |= std::uint64_t{1} << index;
                }
                ByteWriter out;

                out.write<std::uint8_t>(Spawn);
                out.write<std::uint32_t>(id);
                out.write<std::uint16_t>(record.type);
                out.write<std::uint32_t>(record.owner);
                out.write<std::uint64_t>(mask);
                writeComponents(out, record.components, mask);
                ops.push_back(out.bytes());
                continue;
            }
            std::uint64_t changed = 0, removed = 0;

            for (const auto& [index, bytes] : record.components) {
                if (registry.entry(index).mode == Replicate::OnSpawn) {
                    continue;   // told once, when the entity appeared
                }
                const auto old = before->second.components.find(index);

                if (old == before->second.components.end() || old->second != bytes) {
                    changed |= std::uint64_t{1} << index;
                }
            }
            for (const auto& [index, bytes] : before->second.components) {
                if (registry.entry(index).mode != Replicate::OnSpawn && !record.components.count(index)) {
                    removed |= std::uint64_t{1} << index;
                }
            }
            if (changed || removed) {
                ByteWriter out;

                out.write<std::uint8_t>(Update);
                out.write<std::uint32_t>(id);
                out.write<std::uint64_t>(changed);
                out.write<std::uint64_t>(removed);
                writeComponents(out, record.components, changed);
                ops.push_back(out.bytes());
            }
        }
        if (baseline) {
            for (const auto& [id, record] : *baseline) {
                if (!current.count(id)) {
                    ByteWriter out;

                    out.write<std::uint8_t>(Destroy);
                    out.write<std::uint32_t>(id);
                    ops.push_back(out.bytes());
                }
            }
        }
        return ops;
    }

    WorldState applyOps(const ReplicationRegistry& registry, const WorldState& baseline, std::span<const std::uint8_t> ops)
    {
        WorldState state = baseline;
        ByteReader in(ops);

        auto readComponents = [&](std::map<std::uint8_t, Bytes>& into, std::uint64_t mask) {
            if (registry.size() < 64 && (mask >> registry.size()) != 0) {
                throw SerializerError("replication: a component that is not registered");
            }
            for (std::uint8_t index = 0; index < 64; ++index) {
                if (mask & (std::uint64_t{1} << index)) {
                    const std::size_t size = in.read<std::uint16_t>();
                    const auto bytes = in.readBytes(size);

                    into[index] = Bytes(bytes.begin(), bytes.end());
                }
            }
        };

        while (!in.atEnd()) {
            const std::uint8_t kind = in.read<std::uint8_t>();
            const NetworkId id = in.read<std::uint32_t>();

            switch (kind) {
                case Spawn: {
                    EntityRecord record;

                    record.type = in.read<std::uint16_t>();
                    record.owner = in.read<std::uint32_t>();
                    readComponents(record.components, in.read<std::uint64_t>());
                    state[id] = std::move(record);
                    break;
                }
                case Update: {
                    const auto found = state.find(id);
                    const std::uint64_t changed = in.read<std::uint64_t>();
                    const std::uint64_t removed = in.read<std::uint64_t>();
                    std::map<std::uint8_t, Bytes> values;

                    readComponents(values, changed);
                    if (found == state.end()) {
                        throw SerializerError("replication: an update of an entity that is not there");
                    }
                    for (auto& [index, bytes] : values) {
                        found->second.components[index] = std::move(bytes);
                    }
                    for (std::uint8_t index = 0; index < 64; ++index) {
                        if (removed & (std::uint64_t{1} << index)) {
                            found->second.components.erase(index);
                        }
                    }
                    break;
                }
                case Destroy:
                    state.erase(id);
                    break;
                default:
                    throw SerializerError("replication: a record of an unknown kind");
            }
        }
        return state;
    }

}
