#include "ArenaRoom.hpp"
#include <algorithm>

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
        spawnPlayer(world(), player.networkId);
        m_replication->addClient(player.connection);
        m_inputs->addClient(player.connection);
    }

    void ArenaRoom::onPlayerLeft(const Player& player, kuge::net::DisconnectReason)
    {
        auto& respawns = world().getResource<Match>().respawns;

        if (const auto body = playerOf(world(), player.networkId)) {
            world().remove(*body);
        }
        respawns.erase(std::remove_if(respawns.begin(), respawns.end(),
            [&player](const Match::Respawn& respawn) { return respawn.player == player.networkId; }), respawns.end());
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
            // A jump is made once, and a shot too, even if the input is used again because the next one is late
            Steer steer = applied.input;

            steer.jump = steer.jump && !applied.repeated;
            steerPlayer(world, *body, steer);
            if (steer.fire && !applied.repeated) {
                fire(world, *body, steer);
            }
            m_replication->setInputAck(applied.connection, applied.sequence);
        }
    }

    void ArenaRoom::afterPhysics(kw::World& world)
    {
        stepRules(world);
        if (!finishing() && finished(world)) {
            finish(kuge::net::RoomEnd::GameOver);
        }
    }

}
