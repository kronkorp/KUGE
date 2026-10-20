#pragma once

#include "kronkworld/Kronkworld.hpp"
#include <any>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A short memory of some components of the world, to look at it as it was a few ticks ago
     *
     * A server that judges what a player did must look at the world **as that player saw it**: others are drawn about 100 ms in the
     * past (interpolation) plus the player's delay to the server. Keep the components that matter, record after each simulation tick,
     * and ask for the world back then:
     *
     *     kuge::WorldHistory history({.windowTicks = 15});                    // 250 ms at 60 ticks a second
     *     history.keep<kuge::Transform3D>(lerp);                              // the components to remember (a lerp is optional)
     *     history.keep<kuge::Hitboxes>();
     *     ...
     *     history.record(world, tick);                                        // each tick, after the simulation
     *     ...
     *     history.rewound(world, viewTick, [&](const kuge::WorldHistory::Rewound& past) {
     *         hit = physics.raycastHitboxes(world, origin, direction, range, mask,
     *             [&](kw::Entity e) { return !past.existed(e); });            // an entity born since is not there to be hit
     *     });                                                                 // everything is back as it was, even if fn throws
     *
     * Only what is `keep`t is moved. A tick between two recorded ones is **interpolated** when the component has a lerp, else the
     * nearest one is used. What is rewound is the world, not the player who asks: the game decides what it moves back (the shooter
     * itself, and its own movement, should not be rewound: leave it out with `select`, or ignore it).
     *
     * Entities are named by `identity` (their number by default). A World reuses the numbers of entities that were removed: a game that
     * removes entities in the window should give a name that is never reused (the network id of a replicated entity).
     *
     * Not thread-safe: one thread, the room's.
     */
    ////////////////////////////////////////////////////////////////////////////
    class WorldHistory
    {
        public:
            struct Config
            {
                std::size_t windowTicks = 15;   //!< Ticks kept (a fixed number: older ones are forgotten)
                //! Who an entity is, from one tick to the next. Default: the number of the entity.
                std::function<std::uint64_t(kw::World&, kw::Entity)> identity;
            };

            //! What fn is told about the past it is called in
            class Rewound
            {
                public:
                    //! Was this entity there at that time? (One that was made since is not: leave it out of what you ask)
                    bool existed(kw::Entity entity) const { return m_present.count(entity) != 0; }
                    double tick(void) const noexcept { return m_tick; }

                private:
                    friend class WorldHistory;

                    std::map<kw::Entity, bool> m_present;
                    double                     m_tick = 0.0;
            };

            //! How far back a request may go
            struct Limit
            {
                enum class Verdict : std::uint8_t {
                    Ok,        //!< The tick asked for is allowed as it is
                    Clamped,   //!< It was further back than the round trip of the player explains: `tick` is the furthest that is believable
                    TooOld,    //!< It is older than the window: refused
                    Future,    //!< After the current tick: refused
                };

                Verdict verdict = Verdict::Ok;
                double  tick = 0.0;
            };

            explicit WorldHistory(Config config) : m_config(std::move(config)) {}
            WorldHistory(void) : WorldHistory(Config()) {}

            //! Remembers a component (before the first record()). With a lerp, a time between two ticks is a blend.
            template<typename C>
            void keep(std::function<C(const C&, const C&, float)> lerp = {})
            {
                Kept kept;

                kept.collect = [](kw::World& world, std::vector<kw::Entity>& into) {
                    for (kw::Entity entity : world.view<C>()) {
                        into.push_back(entity);
                    }
                };
                kept.has = [](kw::World& world, kw::Entity entity) { return world.has<C>(entity); };
                kept.get = [](kw::World& world, kw::Entity entity) -> std::any { return world.get<C>(entity); };
                kept.set = [](kw::World& world, kw::Entity entity, const std::any& value) {
                    if (world.has<C>(entity)) {
                        world.get<C>(entity) = std::any_cast<const C&>(value);
                    }   // (an entity that lost the component since is left alone)
                };
                if (lerp) {
                    kept.blend = [lerp](const std::any& a, const std::any& b, float t) -> std::any {
                        return lerp(std::any_cast<const C&>(a), std::any_cast<const C&>(b), t);
                    };
                }
                m_kept.push_back(std::move(kept));
            }

            //! Writes down the kept components of the entities for which `select` says yes (all that have one if none).
            //! Call it once per tick with a tick number that goes up.
            void record(kw::World& world, std::uint32_t tick, const std::function<bool(kw::World&, kw::Entity)>& select = {});

            //! Runs fn with the kept components set as they were at `tick`, then puts them back (also if fn throws).
            //! @return  false, and fn is not run, if the tick is not in the window
            bool rewound(kw::World& world, double tick, const std::function<void(const Rewound&)>& fn);

            //! What a request to look at `tick` is worth, for a player whose round trip is `rttSeconds`: the tick it was drawing is at most
            //! the delay of its interpolation plus a round trip (and a tick of slack) behind the current tick. Further back than that
            //! is a lie, or a player who is lagging so much that it should not be favoured: it is brought to the limit.
            //! @param interpolationSeconds  How far behind the newest snapshot the client draws others
            Limit limit(double requestedTick, std::uint32_t currentTick, double rttSeconds, std::uint32_t tickRate, double interpolationSeconds) const;

            std::optional<std::uint32_t> oldest(void) const { return m_frames.empty() ? std::nullopt : std::optional(m_frames.front().tick); }
            std::optional<std::uint32_t> newest(void) const { return m_frames.empty() ? std::nullopt : std::optional(m_frames.back().tick); }
            std::size_t                  size(void) const noexcept { return m_frames.size(); }

        private:
            struct Kept
            {
                std::function<void(kw::World&, std::vector<kw::Entity>&)>         collect;
                std::function<bool(kw::World&, kw::Entity)>                       has;
                std::function<std::any(kw::World&, kw::Entity)>                   get;
                std::function<void(kw::World&, kw::Entity, const std::any&)>      set;
                std::function<std::any(const std::any&, const std::any&, float)>  blend;
            };

            struct Frame
            {
                std::uint32_t                                 tick;
                std::map<std::uint64_t, std::vector<std::any>> values;   //!< By identity, then by kept component (empty: not there)
            };

            std::uint64_t identityOf(kw::World& world, kw::Entity entity) const
            {
                return m_config.identity ? m_config.identity(world, entity) : static_cast<std::uint64_t>(entity);
            }

            Config             m_config;
            std::vector<Kept>  m_kept;
            std::deque<Frame>  m_frames;
    };

}
