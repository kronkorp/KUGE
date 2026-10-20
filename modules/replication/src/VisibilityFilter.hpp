#pragma once

#include "ReplicationServer.hpp"
#include <kronk3d/math/Vector.hpp>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace kuge::replication
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What a client is not sent because it cannot see it: the parts of an interest filter that every game would write again
     *
     * In a game where seeing an enemy through a wall is cheating, the filter of the replication is also the anti-cheat: an entity
     * that a player cannot see must not be in its snapshots, or a hacked client draws it. This class does the work that is the
     * same for every such game; **the rules are the game's**, given as functions:
     *
     * - `viewer` says where the eyes of a client are;
     * - `position` says where an entity is (none: it is not a thing of the world, and is always sent);
     * - `clear` says if the way between two points is free (a raycast of the physics, a grid of walls...);
     * - `always` says what a client always gets (itself, its allies: whatever the game's teams and rules say).
     *
     * What it does: an entity farther than `viewDistance` is not sent; the way from the eye to a few points of the entity (head,
     * chest, feet: `samples`) is tested **once per tick** per pair, and only for the entities near enough; an entity is looked at
     * where it will be in `lookAhead` seconds too (and the eye likewise), so that a player who comes round a corner is sent a few
     * tens of milliseconds **before** it can be seen; and an entity that was visible stays for `graceTicks` after it is hidden, so
     * that one that steps behind a post does not vanish and come back.
     *
     *     VisibilityFilter visibility(config);
     *     server.setFilter(visibility.filter());
     *     ...
     *     visibility.newTick(tick);       // each tick, after the simulation, before server.update(tick)
     *     server.update(tick);
     *
     * It protects what the snapshots hold (positions, states). It does not protect the sound, the shots that the game announces, or
     * anything else the game sends outside the replication. An entity that is not sent has no network id on that client either.
     * It looks at every entity for every client: for hundreds of entities, narrow them down in `position` (a grid) first.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct VisibilityConfig
    {
        using Vec3 = k3::Math::Vector3f;

        struct Viewer
        {
            Vec3 eye;
            Vec3 velocity{};
        };

        struct Placed
        {
            Vec3 position;       //!< The feet, or the origin of the entity: `samples` are offsets from it
            Vec3 velocity{};
        };

        float         viewDistance = 100.0f;
        std::uint32_t graceTicks = 6;         //!< An entity that was visible stays sent this long after it is hidden
        double        lookAhead = 0.1;        //!< Seconds: eye and entity are also tried where they will be (their velocity)
        std::uint32_t tickRate = 60;          //!< For lookAhead
        std::vector<Vec3> samples{Vec3{0.0f, 1.7f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.2f, 0.0f}};   //!< Head, chest, feet

        std::function<std::optional<Viewer>(net::ConnectionId)>                              viewer;
        std::function<std::optional<Placed>(NetworkId)>                                      position;
        std::function<bool(const Vec3& from, const Vec3& to)>                                clear;
        std::function<bool(net::ConnectionId, NetworkId, const EntityRecord&)>               always;
    };

    struct VisibilityStats
    {
        std::uint64_t asked = 0;       //!< Questions the replication put (one per client and entity and snapshot)
        std::uint64_t tooFar = 0;
        std::uint64_t seen = 0;        //!< Visible now
        std::uint64_t kept = 0;        //!< Hidden, but sent for the grace delay
        std::uint64_t hidden = 0;
        std::uint64_t rays = 0;        //!< Calls to `clear`
    };

    class VisibilityFilter
    {
        public:
            explicit VisibilityFilter(VisibilityConfig config) : m_config(std::move(config)) {}

            //! Starts a tick: the positions are read again, and what was worked out for the last tick is forgotten
            void newTick(std::uint32_t tick);

            bool operator()(net::ConnectionId connection, NetworkId id, const EntityRecord& record);

            //! As the filter of a ReplicationServer (this object must outlive the server)
            ReplicationServer::Filter filter(void)
            {
                return [this](net::ConnectionId connection, NetworkId id, const EntityRecord& record) { return (*this)(connection, id, record); };
            }

            const VisibilityStats& stats(void) const noexcept { return m_stats; }

        private:
            using Key = std::pair<net::ConnectionId, NetworkId>;

            bool canSee(const VisibilityConfig::Viewer& viewer, const VisibilityConfig::Placed& target);

            VisibilityConfig                                       m_config;
            std::uint32_t                                          m_tick = 0;
            std::map<net::ConnectionId, std::optional<VisibilityConfig::Viewer>> m_viewers;   //!< This tick
            std::map<NetworkId, std::optional<VisibilityConfig::Placed>>         m_placed;    //!< This tick
            std::map<Key, bool>                                    m_answers;                 //!< This tick
            std::map<Key, std::uint32_t>                           m_lastSeen;                //!< The last tick each pair was visible
            VisibilityStats                                        m_stats;
    };

}
