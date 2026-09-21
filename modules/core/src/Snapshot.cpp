#include "Snapshot.hpp"
#include <algorithm>
#include <format>
#include <stdexcept>

namespace
{
    constexpr std::uint32_t MAGIC = kuge::fourcc('K', 'S', 'N', 'P');
    constexpr std::uint16_t FORMAT = 1;

    // A value is written with its size in front: what is not known can be skipped
    void writeBlock(kuge::ByteWriter& out, const kuge::ByteWriter& block)
    {
        out.write<std::uint32_t>(static_cast<std::uint32_t>(block.size()));
        out.writeBytes(block.bytes());
    }
}

void kuge::SnapshotRegistry::checkNew(const std::string& name) const
{
    const auto same = [&name](const auto& entry) { return entry.name == name; };

    if (name.empty()) {
        throw std::invalid_argument("a name is needed to save a type");
    }
    if (std::any_of(m_components.begin(), m_components.end(), same) ||
        std::any_of(m_resources.begin(), m_resources.end(), same)) {
        throw std::invalid_argument(std::format("'{}' is already registered", name));
    }
}

void kuge::SnapshotRegistry::save(kw::World& world, ByteWriter& out) const
{
    std::vector<kw::Entity> entities;

    out.writeHeader(MAGIC, FORMAT);

    // Resources, in the order they were registered
    std::vector<const ResourceEntry*> present;
    for (const ResourceEntry& entry : m_resources) {
        if (entry.has(world)) {
            present.push_back(&entry);
        }
    }
    out.write<std::uint32_t>(static_cast<std::uint32_t>(present.size()));
    for (const ResourceEntry* entry : present) {
        ByteWriter block;

        entry->write(world, block);
        out.writeString(entry->name);
        writeBlock(out, block);
    }

    // Entities by increasing number: the ECS does not promise any order
    auto view = world.view<Persistent>();
    for (kw::Entity entity : view) {
        entities.push_back(entity);
    }
    std::sort(entities.begin(), entities.end());
    out.write<std::uint32_t>(static_cast<std::uint32_t>(entities.size()));
    for (kw::Entity entity : entities) {
        std::vector<const Entry*> held;

        for (const Entry& entry : m_components) {
            if (entry.has(world, entity)) {
                held.push_back(&entry);
            }
        }
        out.write<std::uint32_t>(static_cast<std::uint32_t>(held.size()));
        for (const Entry* entry : held) {
            ByteWriter block;

            entry->write(world, entity, block);
            out.writeString(entry->name);
            writeBlock(out, block);
        }
    }
}

std::vector<kw::Entity> kuge::SnapshotRegistry::load(kw::World& world, ByteReader& in) const
{
    if (in.readHeader(MAGIC) != FORMAT) {
        throw SerializerError("SnapshotRegistry: unknown format of snapshot");
    }
    // First everything is read into things to do: nothing touches the World
    // until the whole snapshot is known to be good
    std::vector<ApplyResource> resources;
    std::vector<std::vector<Apply>> entities;
    const std::uint32_t resourceCount = in.read<std::uint32_t>();

    for (std::uint32_t i = 0; i < resourceCount; ++i) {
        const std::string name = in.readString();
        const std::uint32_t size = in.read<std::uint32_t>();
        ByteReader block(in.readBytes(size));
        const auto found = std::find_if(m_resources.begin(), m_resources.end(), [&](const ResourceEntry& e) { return e.name == name; });

        if (found != m_resources.end()) {
            resources.push_back(found->read(block));
        }
    }
    const std::uint32_t entityCount = in.read<std::uint32_t>();

    for (std::uint32_t i = 0; i < entityCount; ++i) {
        const std::uint32_t componentCount = in.read<std::uint32_t>();
        std::vector<Apply> components;

        for (std::uint32_t j = 0; j < componentCount; ++j) {
            const std::string name = in.readString();
            const std::uint32_t size = in.read<std::uint32_t>();
            ByteReader block(in.readBytes(size));
            const auto found = std::find_if(m_components.begin(), m_components.end(), [&](const Entry& e) { return e.name == name; });

            if (found != m_components.end()) {
                components.push_back(found->read(block));
            }
        }
        entities.push_back(std::move(components));
    }
    // It is all good: now it is done
    std::vector<kw::Entity> created;

    for (const ApplyResource& apply : resources) {
        apply(world);
    }
    for (const std::vector<Apply>& components : entities) {
        const kw::Entity entity = world.create();

        world.add<Persistent>(entity, Persistent{});
        for (const Apply& apply : components) {
            apply(world, entity);
        }
        created.push_back(entity);
    }
    return created;
}
