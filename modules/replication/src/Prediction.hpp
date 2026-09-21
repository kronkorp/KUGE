#pragma once

#include "Input.hpp"
#include "ReplicationClient.hpp"
#include "Transform2D.hpp"
#include <cmath>
#include <deque>
#include <functional>
#include <memory>

namespace kuge::replication
{

    template<typename Input>
    struct PredictionConfig
    {
        //! Makes the world that the client simulates by itself: what the game needs to move the player (its physics,
        //! the level), and the player's entity. The room builds the same, with the same code.
        std::function<kw::Entity(kw::World&)>                       build;
        //! Puts an input into the entity: its velocity, its wish... (the room uses the same function)
        std::function<void(kw::World&, kw::Entity, const Input&)>   apply;
        //! One tick of the simulation of that world (the physics, for example)
        std::function<void(kw::World&, double dt)>                  step;
        double        dt             = 1.0 / 60.0;
        std::size_t   redundancy     = 4;        //!< Inputs in each packet: the newest and the ones before, in case they were lost
        std::size_t   maxPending     = 128;      //!< Inputs kept while the server does not answer
        double        smoothing      = 0.1;      //!< Seconds that a correction takes to be absorbed
        float         snapDistance   = 64.0f;    //!< A correction bigger than this (in pixels) is not smoothed: the player is put there
    };

    struct PredictionStats
    {
        std::uint64_t ticks = 0;
        std::uint64_t reconciliations = 0;   //!< Snapshots that were used to correct the prediction
        std::uint64_t replays = 0;           //!< Inputs simulated again
        std::uint64_t corrections = 0;       //!< Reconciliations that changed where the player is (by more than a hair)
        std::uint64_t snaps = 0;             //!< ... by more than snapDistance
        float         largestCorrection = 0.0f;
        std::size_t   pending = 0;           //!< Inputs that the server has not confirmed
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Makes the player's own entity answer at once, without waiting for
     *         the server, and puts it right when the server says otherwise
     *
     *     PredictionConfig<Steer> config;
     *     config.build = [](kw::World& world) { ...; return player; };
     *     config.apply = [](kw::World& world, kw::Entity e, const Steer& steer) { ... };
     *     config.step = [](kw::World& world, double dt) { world.getResource<kuge::Physics2D>().step(world, float(dt)); };
     *
     *     Prediction<Steer> prediction(config, world, registry, replication, room);
     *     replication.setLocalPlayer(welcome.networkId);
     *     ...
     *     prediction.tick(steer);                    // each fixed tick, with what the player is doing
     *
     * **Each tick**: the input gets a number, is simulated on the private world, and the result is
     * copied to the entity that the game draws; the last few inputs go to the server.
     * **Each snapshot**: it carries the state of the entity with the number of the last input that state
     * includes. The private entity is put in that state, the inputs after it are simulated again, and
     * that is the new prediction. If the server is in agreement, nothing changes. If not (something the
     * client cannot know: another player, a rule), the difference is not shown at once: the entity keeps
     * being drawn where it was and slides to the new place over `smoothing` seconds, unless it is
     * more than `snapDistance` away, when it is put there.
     *
     * What is predicted is the components that the registry marks `predicted` (Transform2D, Body...).
     * The private world is only the player: it never contains the other entities, which the client only
     * draws.
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename Input>
    class Prediction
    {
        public:
            Prediction(PredictionConfig<Input> config, kw::World& world, const ReplicationRegistry& registry,
                ReplicationClient& replication, net::Endpoint& room)
                : m_config(std::move(config)), m_world(world), m_registry(registry), m_room(room)
            {
                for (std::size_t i = 0; i < registry.size(); ++i) {
                    if (registry.entry(i).predicted) {
                        m_predicted.push_back(i);
                    }
                }
                replication.onOwned([this, alive = m_alive](kw::Entity entity, const EntityRecord& record, std::uint32_t, std::uint32_t inputAck) {
                    if (*alive) {
                        reconcile(entity, record, inputAck);
                    }
                });
            }

            ~Prediction(void) { *m_alive = false; }

            Prediction(const Prediction&)            = delete;
            Prediction& operator=(const Prediction&) = delete;

            //! Once per fixed tick. Does nothing until the server has shown the player's entity.
            void tick(const Input& input)
            {
                if (!m_ready) {
                    return;
                }
                ++m_stats.ticks;
                m_pending.push_back(Pending{++m_sequence, input});
                while (m_pending.size() > m_config.maxPending) {
                    m_pending.pop_front();
                }
                simulate(input);
                m_offset = m_offset * static_cast<float>(std::exp(-m_config.dt / std::max(m_config.smoothing, 1e-6)));
                if (std::fabs(m_offset.x) + std::fabs(m_offset.y) < 0.01f) {
                    m_offset = {};
                }
                publish();
                send();
                m_stats.pending = m_pending.size();
            }

            //! The entity the game draws, once the server has shown it
            std::optional<kw::Entity> entity(void) const { return m_ready ? std::optional<kw::Entity>(m_visible) : std::nullopt; }
            bool                      ready(void) const noexcept { return m_ready; }
            std::uint32_t             sequence(void) const noexcept { return m_sequence; }
            const PredictionStats&    stats(void) const noexcept { return m_stats; }
            kw::World&                privateWorld(void) noexcept { return m_private; }
            kw::Entity                privateEntity(void) const noexcept { return m_player; }
            Vec2                      offset(void) const noexcept { return m_offset; }

        private:
            struct Pending
            {
                std::uint32_t sequence;
                Input         input;
            };

            void simulate(const Input& input)
            {
                m_config.apply(m_private, m_player, input);
                m_config.step(m_private, m_config.dt);
            }

            // The predicted state of the private entity, into the one that is drawn (with what is left of the last correction)
            void publish(void)
            {
                for (const std::size_t index : m_predicted) {
                    const auto& entry = m_registry.entry(index);

                    if (entry.has(m_private, m_player)) {
                        ByteWriter out;

                        entry.write(m_private, m_player, out);
                        entry.apply(m_world, m_visible, out.bytes());
                    }
                }
                if (m_world.has<Transform2D>(m_visible) && m_private.has<Transform2D>(m_player)) {
                    m_world.get<Transform2D>(m_visible).position = m_private.get<Transform2D>(m_player).position + m_offset;
                }
            }

            void send(void)
            {
                if (!m_room.connected()) {
                    return;
                }
                const std::size_t count = std::min(m_pending.size(), m_config.redundancy);
                InputPacket packet;

                packet.firstSequence = m_pending[m_pending.size() - count].sequence;
                packet.count = static_cast<std::uint8_t>(count);
                for (std::size_t i = m_pending.size() - count; i < m_pending.size(); ++i) {
                    ByteWriter one;

                    net::encode(one, m_pending[i].input);
                    packet.data.push_back(static_cast<std::uint8_t>(one.size() & 0xFF));
                    packet.data.push_back(static_cast<std::uint8_t>(one.size() >> 8));
                    packet.data.insert(packet.data.end(), one.bytes().begin(), one.bytes().end());
                }
                m_room.send(m_room.connections().front(), packet, net::Channel::Unreliable);
            }

            // The server's word about the entity: put the private one there, and simulate again what the server has not seen
            void reconcile(kw::Entity entity, const EntityRecord& record, std::uint32_t inputAck)
            {
                const bool first = !m_ready;

                if (first) {
                    m_visible = entity;
                    m_player = m_config.build(m_private);
                    m_sequence = inputAck;   // (the server counts from where this client's inputs are)
                }
                const Vec2 before = m_private.has<Transform2D>(m_player) ? m_private.get<Transform2D>(m_player).position : Vec2{};

                for (const std::size_t index : m_predicted) {
                    const auto found = record.components.find(static_cast<std::uint8_t>(index));

                    if (found != record.components.end()) {
                        m_registry.entry(index).apply(m_private, m_player, found->second);
                    }
                }
                while (!m_pending.empty() && m_pending.front().sequence <= inputAck) {
                    m_pending.pop_front();
                }
                for (const Pending& pending : m_pending) {
                    simulate(pending.input);
                    ++m_stats.replays;
                }
                ++m_stats.reconciliations;
                if (!first && m_private.has<Transform2D>(m_player)) {
                    const Vec2 after = m_private.get<Transform2D>(m_player).position;
                    const Vec2 shown = before + m_offset;     // where the player is drawn: it must not jump
                    const Vec2 gap = shown - after;
                    const Vec2 wrong = before - after;        // how wrong the last prediction was
                    const float error = std::sqrt(wrong.x * wrong.x + wrong.y * wrong.y);
                    const float jump = std::sqrt(gap.x * gap.x + gap.y * gap.y);

                    if (error > 0.05f) {
                        ++m_stats.corrections;
                        m_stats.largestCorrection = std::max(m_stats.largestCorrection, error);
                    }
                    if (jump > m_config.snapDistance) {
                        m_offset = {};
                        ++m_stats.snaps;
                    } else {
                        m_offset = gap;
                    }
                }
                m_ready = true;
                publish();
                m_stats.pending = m_pending.size();
            }

            PredictionConfig<Input>        m_config;
            kw::World&                     m_world;
            const ReplicationRegistry&     m_registry;
            net::Endpoint&                 m_room;
            std::vector<std::size_t>       m_predicted;
            kw::World                      m_private;
            kw::Entity                     m_player{};
            kw::Entity                     m_visible{};
            std::deque<Pending>            m_pending;
            std::uint32_t                  m_sequence = 0;
            Vec2                           m_offset{};
            bool                           m_ready = false;
            PredictionStats                m_stats;
            std::shared_ptr<bool>          m_alive = std::make_shared<bool>(true);
    };

}
