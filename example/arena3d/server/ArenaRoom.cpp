#include "ArenaRoom.hpp"

namespace arena
{

    // A system made of a function
    class ArenaRoom::Simulate final : public kw::ISystem
    {
        public:
            explicit Simulate(std::function<void(kw::World&)> work) : m_work(std::move(work)) {}

            bool handle(kw::World& world) override { m_work(world); return true; }

        private:
            std::function<void(kw::World&)> m_work;
    };

    void ArenaRoom::onRoomEnter(void)
    {
        m_registry = makeRegistry();
        buildLevel(world());
        world().addResource<Match>();
        m_replication = std::make_unique<kuge::replication::ReplicationServer>(world(), m_registry, endpoint());
        m_inputs = std::make_unique<kuge::replication::InputServer<Steer>>(endpoint(), kuge::replication::InputServerConfig{.jitter = 2});   // (two inputs of margin: the clocks of the client and the room do not agree)
        world().getResource<Match>().track = [this](kw::Entity entity, kuge::replication::EntityType type, std::uint32_t owner) {
            m_replication->track(entity, type, owner);
        };
        buildGame(world());
        addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Simulate>([this](kw::World& w) { applyInputs(w); }));
        addSystem(kw::Schedule::Fixed, kuge::stage::Physics, std::make_unique<Simulate>([](kw::World& w) { stepWorld(w, TICK_DT); }));
        addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<Simulate>([this](kw::World& w) { afterPhysics(w); }));
        addSystem(kw::Schedule::Fixed, kuge::stage::Replication, std::make_unique<Simulate>([this](kw::World& w) {
            m_replication->update(static_cast<std::uint32_t>(w.getResource<kuge::Time>().tick + 1));
        }));
    }

    void ArenaRoom::onPlayerJoined(const Player& player)
    {
        // Players start in a row, facing the same way: the first one has a coin right ahead
        static constexpr float START_X[4] = {0.0f, 2.0f, -2.0f, 4.0f};
        const float x = START_X[(player.networkId - 1) % 4];
        const kw::Entity body = buildPlayer(world(), {x, 1.0f, 6.0f});

        world().add<Score>(body, Score{});
        world().add<Owned>(body, Owned{player.networkId});
        m_replication->track(body, PLAYER, player.networkId);
        m_replication->addClient(player.connection);
        m_inputs->addClient(player.connection);
        m_anyPlayer = true;
    }

    void ArenaRoom::onPlayerLeft(const Player& player, kuge::net::DisconnectReason)
    {
        if (const auto body = playerOf(world(), player.networkId)) {
            world().remove(*body);
        }
        m_replication->removeClient(player.connection);
        m_inputs->removeClient(player.connection);
    }

    void ArenaRoom::applyInputs(kw::World& world)
    {
        for (const auto& applied : m_inputs->collect()) {
            const Player* who = player(applied.connection);
            const auto body = who ? playerOf(world, who->networkId) : std::nullopt;

            if (!body) {
                continue;
            }
            // A jump is made once, even if the input is used again because the next one is late
            Steer steer = applied.input;

            steer.jump = steer.jump && !applied.repeated;
            steerPlayer(world, *body, steer);
            m_replication->setInputAck(applied.connection, applied.sequence);
        }
    }

    void ArenaRoom::afterPhysics(kw::World& world)
    {
        collectCoins(world);
        if (!finishing() && m_anyPlayer && !players().empty() && outcome(world) == Outcome::Won) {
            finish(kuge::net::RoomEnd::GameOver);
        }
    }

}
