#pragma once

#include "Serializer.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace kuge
{

    //! Marks an entity to be saved by a SnapshotRegistry
    struct Persistent {};

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What to keep of a World, and how: the components and the
     *         resources that a save holds
     *
     * The game says, for each type it wants saved, how to write it and how to
     * read it back. Nothing is saved that was not registered, so a save never
     * holds a texture or a pointer by accident.
     *
     *     kuge::SnapshotRegistry registry;
     *     registry.component<Health>("health",
     *         [](kuge::ByteWriter& out, const Health& h) { out.write(h.current); out.write(h.max); },
     *         [](kuge::ByteReader& in) { return Health{in.read<int>(), in.read<int>()}; });
     *     registry.resource<Score>("score", ...);
     *
     *     world.add<kuge::Persistent>(hero, {});        // this one is saved
     *     registry.save(world, writer);                 // then give writer.bytes() to a SaveSlots
     *     registry.load(world, reader);                 // creates the entities again
     *
     * Only entities that have a Persistent are saved, with the registered
     * components they have. Loading creates new entities (their numbers are not
     * kept), so a component must not hold the number of another entity.
     *
     * The bytes are always the same for the same world, whatever the ECS does
     * with its storage: entities go by increasing number, components in the
     * order they were registered.
     *
     * A name that the reader does not know is skipped, so a save from a newer
     * version of the game (with more components) still loads. Loading is all or
     * nothing: if the data is damaged, the World is not touched.
     */
    ////////////////////////////////////////////////////////////////////////////
    class SnapshotRegistry
    {
        public:
            //! @param write  Writes the value
            //! @param read   Reads it back; may throw SerializerError
            template<typename C>
            void component(std::string name, std::function<void(ByteWriter&, const C&)> write,
                std::function<C(ByteReader&)> read)
            {
                checkNew(name);
                Entry entry;

                entry.name = std::move(name);
                entry.has = [](kw::World& world, kw::Entity e) { return world.has<C>(e); };
                entry.write = [write](kw::World& world, kw::Entity e, ByteWriter& out) { write(out, world.get<C>(e)); };
                entry.read = [read](ByteReader& in) -> Apply {
                    C value = read(in);

                    return [value](kw::World& world, kw::Entity e) { world.add<C>(e, value); };
                };
                m_components.push_back(std::move(entry));
            }

            template<typename R>
            void resource(std::string name, std::function<void(ByteWriter&, const R&)> write,
                std::function<R(ByteReader&)> read)
            {
                checkNew(name);
                ResourceEntry entry;

                entry.name = std::move(name);
                entry.has = [](kw::World& world) {
                    try {
                        world.getResource<R>();
                        return true;
                    } catch (const kw::ResourceError&) {
                        return false;
                    }
                };
                entry.write = [write](kw::World& world, ByteWriter& out) { write(out, world.getResource<R>()); };
                entry.read = [read](ByteReader& in) -> ApplyResource {
                    R value = read(in);

                    return [value](kw::World& world) { world.addResource<R>(value); };
                };
                m_resources.push_back(std::move(entry));
            }

            //! Writes the registered resources, and the Persistent entities
            void save(kw::World& world, ByteWriter& out) const;

            //! Puts back what save() wrote, as new entities
            //! @return  The entities created, in the order they were saved
            //! @throw SerializerError if the data is damaged (the World is unchanged)
            std::vector<kw::Entity> load(kw::World& world, ByteReader& in) const;

        private:
            using Apply         = std::function<void(kw::World&, kw::Entity)>;
            using ApplyResource = std::function<void(kw::World&)>;

            struct Entry
            {
                std::string                                    name;
                std::function<bool(kw::World&, kw::Entity)>    has;
                std::function<void(kw::World&, kw::Entity, ByteWriter&)> write;
                std::function<Apply(ByteReader&)>              read;
            };

            struct ResourceEntry
            {
                std::string                                    name;
                std::function<bool(kw::World&)>                has;
                std::function<void(kw::World&, ByteWriter&)>   write;
                std::function<ApplyResource(ByteReader&)>      read;
            };

            void checkNew(const std::string& name) const;

            std::vector<Entry>          m_components;
            std::vector<ResourceEntry>  m_resources;
    };

}
