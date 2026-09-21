#pragma once

#include "Serializer.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace kuge::replication
{

    //! What names an entity that exists on the server, and on every client that sees it
    using NetworkId  = std::uint32_t;
    //! What the game says an entity is (a ship, a bullet...): what a client builds from
    using EntityType = std::uint16_t;

    using Bytes = std::vector<std::uint8_t>;

    //! On an entity that is replicated (the server puts it with track(), the client when it makes the entity)
    struct Replicated
    {
        NetworkId  id    = 0;
        EntityType type  = 0;
        NetworkId  owner = 0;   //!< The player it belongs to (a network id of the room, 0: nobody)
    };

    //! How a component goes to the clients
    enum class Replicate : std::uint8_t {
        Interpolated,   //!< Changes all the time (positions): the client keeps the last values and draws between them
        OnChange,       //!< Sent when it changes, applied at once (health, score...)
        OnSpawn,        //!< Sent once, when the client learns of the entity (team, colour...)
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Which components are replicated, and how: the same list on the
     *         server and on the clients (build it with one function that both call)
     *
     *     kuge::replication::ReplicationRegistry registry;
     *
     *     kuge::replication::registerTransform2D(registry, kuge::replication::Replicate::Interpolated);
     *     registry.component<Health>("health", kuge::replication::Replicate::OnChange,
     *         [](kuge::ByteWriter& out, const Health& h) { out.write(h.points); },
     *         [](kuge::ByteReader& in) { return Health{in.read<int>()}; });
     *
     * The order of the registrations is the number of a component on the wire, so both
     * sides must register the same, in the same order: a hash of the list (names, modes,
     * order) travels with the snapshots, and a client ignores those of a server whose
     * list differs. At most 64 components.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ReplicationRegistry
    {
        public:
            struct Entry
            {
                std::string  name;
                Replicate    mode = Replicate::OnChange;
                bool         predicted = false;   //!< Part of the state that a client simulates itself for its own entity

                std::function<bool(kw::World&, kw::Entity)>                        has;
                std::function<void(kw::World&, kw::Entity, ByteWriter&)>           write;
                //! Reads (checked), and adds or replaces the component
                std::function<void(kw::World&, kw::Entity, std::span<const std::uint8_t>)> apply;
                std::function<void(kw::World&, kw::Entity)>                        remove;
                //! Between two values (bytes of the codec), the result put in the entity. Empty if the component has no lerp.
                std::function<void(kw::World&, kw::Entity, std::span<const std::uint8_t>, std::span<const std::uint8_t>, float)> interpolate;
            };

            static constexpr std::size_t MAX_REPLICATED = 64;

            //! @param write        Puts the value in the buffer
            //! @param read         Reads it back (may throw SerializerError)
            //! @param lerp         Between two values, for Replicate::Interpolated
            //! @param predicted    A client that owns the entity simulates this component itself (see Prediction)
            template<typename C>
            std::size_t component(std::string name, Replicate mode,
                std::function<void(ByteWriter&, const C&)> write,
                std::function<C(ByteReader&)> read,
                std::function<C(const C&, const C&, float)> lerp = {},
                bool predicted = false)
            {
                if (mode == Replicate::Interpolated && !lerp) {
                    throw std::logic_error("ReplicationRegistry: an Interpolated component needs a lerp");
                }
                if (m_entries.size() >= MAX_REPLICATED) {
                    throw std::length_error("ReplicationRegistry: at most 64 components");
                }
                for (const auto& known : m_entries) {
                    if (known.name == name) {
                        throw std::logic_error("ReplicationRegistry: '" + name + "' is registered twice");
                    }
                }
                Entry entry;

                entry.name = std::move(name);
                entry.mode = mode;
                entry.predicted = predicted;
                entry.has = [](kw::World& world, kw::Entity e) { return world.has<C>(e); };
                entry.write = [write](kw::World& world, kw::Entity e, ByteWriter& out) { write(out, world.get<C>(e)); };
                entry.apply = [read](kw::World& world, kw::Entity e, std::span<const std::uint8_t> bytes) {
                    ByteReader in(bytes);
                    C value = read(in);

                    if (!in.atEnd()) {
                        throw SerializerError("replication: a component longer than its fields");
                    }
                    if (world.has<C>(e)) {
                        world.get<C>(e) = std::move(value);
                    } else {
                        world.add<C>(e, std::move(value));
                    }
                };
                entry.remove = [](kw::World& world, kw::Entity e) { if (world.has<C>(e)) { world.remove<C>(e); } };
                if (lerp) {
                    entry.interpolate = [read, lerp](kw::World& world, kw::Entity e, std::span<const std::uint8_t> a, std::span<const std::uint8_t> b, float t) {
                        ByteReader inA(a), inB(b);
                        C value = lerp(read(inA), read(inB), t);

                        if (world.has<C>(e)) {
                            world.get<C>(e) = std::move(value);
                        } else {
                            world.add<C>(e, std::move(value));
                        }
                    };
                }
                m_entries.push_back(std::move(entry));
                return m_entries.size() - 1;
            }

            std::size_t        size(void) const noexcept { return m_entries.size(); }
            const Entry&       entry(std::size_t index) const { return m_entries.at(index); }

            //! A number that both sides compute from their list: equal, or they do not speak the same language
            std::uint32_t      schema(void) const;

        private:
            std::vector<Entry> m_entries;
    };

}
