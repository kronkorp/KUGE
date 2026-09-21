#include "RTypeRoom.hpp"

namespace rtype
{

    // A system made of a function
    class RTypeRoom::Simulate final : public kw::ISystem
    {
        public:
            explicit Simulate(std::function<void(kw::World&)> work) : m_work(std::move(work)) {}
            bool handle(kw::World& world) override { m_work(world); return true; }

        private:
            std::function<void(kw::World&)> m_work;
    };

    void RTypeRoom::onRoomEnter(void)
    {
        m_registry = makeRegistry();
        buildArena(world());
        world().addResource<Match>();
        world().getResource<Match>().rng.seed(init().roomId);
        m_replication = std::make_unique<kuge::replication::ReplicationServer>(world(), m_registry, endpoint());
        m_inputs = std::make_unique<kuge::replication::InputServer<Steer>>(endpoint(), kuge::replication::InputServerConfig{.jitter = 2});   // (two inputs of margin: the clocks of the client and the room do not agree)
        world().getResource<Match>().track = [this](kw::Entity entity, kuge::replication::EntityType type, std::uint32_t owner) {
            m_replication->track(entity, type, owner);
        };
        addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Simulate>([this](kw::World& w) { applyInputs(w); }));
        addSystem(kw::Schedule::Fixed, kuge::stage::Physics, std::make_unique<Simulate>([](kw::World& w) { stepArena(w, TICK_DT); }));
        addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<Simulate>([this](kw::World& w) { afterPhysics(w); }));
        addSystem(kw::Schedule::Fixed, kuge::stage::Replication, std::make_unique<Simulate>([this](kw::World& w) {
            m_replication->update(static_cast<std::uint32_t>(w.getResource<kuge::Time>().tick + 1));
        }));
    }

    void RTypeRoom::onPlayerJoined(const Player& player)
    {
        const kw::Entity ship = buildShip(world(), {40.0f, 60.0f + 70.0f * static_cast<float>((player.networkId - 1) % 4)});

        world().add<Health>(ship, Health{settings().shipHealth});
        world().add<Score>(ship, Score{});
        world().add<Slot>(ship, Slot{static_cast<std::uint8_t>((player.networkId - 1) % 4)});
        world().add<Gun>(ship, Gun{});
        world().add<Owned>(ship, Owned{player.networkId});
        m_replication->track(ship, SHIP, player.networkId);
        m_replication->addClient(player.connection);
        m_inputs->addClient(player.connection);
        m_ships[player.connection] = ship;
        m_anyShip = true;
    }

    void RTypeRoom::onPlayerLeft(const Player& player, kuge::net::DisconnectReason)
    {
        const auto found = m_ships.find(player.connection);

        if (found != m_ships.end()) {
            if (world().has<Gun>(found->second)) {
                world().remove(found->second);
            }
            m_ships.erase(found);
        }
        m_replication->removeClient(player.connection);
        m_inputs->removeClient(player.connection);
    }

    void RTypeRoom::applyInputs(kw::World& world)
    {
        for (const auto& applied : m_inputs->collect()) {
            const auto found = m_ships.find(applied.connection);

            if (found == m_ships.end() || !world.has<Gun>(found->second)) {
                continue;
            }
            steerShip(world, found->second, applied.input);
            if (applied.input.fire && !applied.repeated) {
                fire(world, found->second);
            }
            m_replication->setInputAck(applied.connection, applied.sequence);
        }
    }

    void RTypeRoom::afterPhysics(kw::World& world)
    {
        stepRules(world, TICK_DT);
        if (!finishing() && !players().empty() && outcome(world, m_anyShip) != Outcome::Playing) {
            finish(kuge::net::RoomEnd::GameOver);
        }
    }

}
