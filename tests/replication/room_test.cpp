extern "C" {
    #include "kronklab/kronklab.h"
}
#include "GameServer.hpp"
#include "Matchmaking.hpp"
#include "prediction_fixture.hpp"
#include "Stage.hpp"
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

// NOTE: kronklab test names are limited to 31 characters.
// The whole stack: a server with a room that replicates its world, and two clients that predict themselves
// and see each other, in real time, over a loopback network of the process.

namespace
{
    using namespace kuge;
    using Clock = std::chrono::steady_clock;

    // What the room saw, for the test to compare with (the room runs on its own thread)
    struct RoomFacts
    {
        std::mutex                                 mutex;
        std::map<std::uint32_t, Vec2>              positions;   //!< By network id of the player
        std::size_t                                players = 0;
        std::uint64_t                              snapshots = 0;
    };

    RoomFacts& facts(void)
    {
        static RoomFacts value;

        return value;
    }

    // Applies the inputs, moves the world, and shows it to the clients
    class RoomStepper : public kw::ISystem
    {
        public:
            explicit RoomStepper(std::function<void(kw::World&)> work) : m_work(std::move(work)) {}
            bool handle(kw::World& world) override { m_work(world); return true; }

        private:
            std::function<void(kw::World&)> m_work;
    };

    class ReplicatedRoom : public server::RoomScene
    {
        public:
            using RoomScene::RoomScene;

        protected:
            void onRoomEnter(void) override
            {
                m_registry = makePredictionRegistry();
                buildLevel(world());
                m_replication = std::make_unique<replication::ReplicationServer>(world(), m_registry, endpoint());
                m_inputs = std::make_unique<replication::InputServer<Steer>>(endpoint());
                addSystem(kw::Schedule::Fixed, stage::Simulation, std::make_unique<RoomStepper>([this](kw::World& w) {
                    for (const auto& applied : m_inputs->collect()) {
                        const auto found = m_entities.find(applied.connection);

                        if (found != m_entities.end()) {
                            steerPlayer(w, found->second, applied.input);
                            m_replication->setInputAck(applied.connection, applied.sequence);
                        }
                    }
                }));
                addSystem(kw::Schedule::Fixed, stage::Physics, std::make_unique<RoomStepper>([](kw::World& w) { stepPhysics(w, 1.0 / 60.0); }));
                addSystem(kw::Schedule::Fixed, stage::Replication, std::make_unique<RoomStepper>([this](kw::World& w) {
                    m_replication->update(static_cast<std::uint32_t>(ctx().time().tick + 1));
                    std::lock_guard lock(facts().mutex);

                    ++facts().snapshots;
                    for (const auto& [connection, entity] : m_entities) {
                        if (const Player* who = player(connection)) {
                            facts().positions[who->networkId] = w.get<Transform2D>(entity).position;
                        }
                    }
                }));
            }

            void onPlayerJoined(const Player& who) override
            {
                const kw::Entity entity = buildPlayer(world());

                world().get<Transform2D>(entity).position = {50.0f + 40.0f * static_cast<float>(who.networkId), 50.0f};
                m_replication->track(entity, PLAYER, who.networkId);
                m_replication->addClient(who.connection);
                m_inputs->addClient(who.connection);
                m_entities[who.connection] = entity;
                std::lock_guard lock(facts().mutex);
                facts().players = m_entities.size();
            }

            void onPlayerLeft(const Player& who, net::DisconnectReason) override
            {
                const auto found = m_entities.find(who.connection);

                if (found != m_entities.end()) {
                    world().remove(found->second);
                    m_entities.erase(found);
                }
                m_replication->removeClient(who.connection);
                m_inputs->removeClient(who.connection);
                std::lock_guard lock(facts().mutex);
                facts().players = m_entities.size();
                facts().positions.erase(who.networkId);
            }

        private:
            replication::ReplicationRegistry                          m_registry;
            std::unique_ptr<replication::ReplicationServer>           m_replication;
            std::unique_ptr<replication::InputServer<Steer>>          m_inputs;
            std::map<net::ConnectionId, kw::Entity>                   m_entities;
    };

    // A client: joins, predicts its player, draws the others
    struct Player
    {
        net::Net                                  net;
        net::MatchmakingClient                    matchmaking{net};
        replication::ReplicationRegistry          registry = makePredictionRegistry();
        kw::World                                 world;
        std::unique_ptr<replication::ReplicationClient> replication;
        std::unique_ptr<replication::Prediction<Steer>> prediction;
        std::uint32_t                             networkId = 0;
        Steer                                     steer;

        Player(server::GameServer&, net::LoopbackNetwork& network, const char* name)
        {
            matchmaking.onJoined([this](net::Endpoint& room, const net::Welcome& welcome) {
                networkId = welcome.networkId;
                replication = std::make_unique<replication::ReplicationClient>(world, registry);
                replication->setLocalPlayer(welcome.networkId);
                replication->predictType(PLAYER);
                replication->attach(room);
                replication::PredictionConfig<Steer> config;

                config.build = [](kw::World& w) { buildLevel(w); return buildPlayer(w); };
                config.apply = &steerPlayer;
                config.step = &stepPhysics;
                prediction = std::make_unique<replication::Prediction<Steer>>(config, world, registry, *replication, room);
            });
            matchmaking.connectLobby("lobby", network);
            matchmaking.join("arena", name);
        }

        // One tick of the client, as its scene would
        void tick(void)
        {
            net.poll();
            if (prediction) {
                prediction->tick(steer);
                replication->update(1.0 / 60.0);
            }
        }

        Vec2 position(void) { return world.get<Transform2D>(*prediction->entity()).position; }
    };

    // Runs the clients at 60 Hz for a while
    void runFor(std::vector<Player*> players, double seconds)
    {
        const auto end = Clock::now() + std::chrono::duration<double>(seconds);
        auto next = Clock::now();

        while (Clock::now() < end) {
            for (Player* player : players) {
                player->tick();
            }
            next += std::chrono::microseconds(16667);
            std::this_thread::sleep_until(next);
        }
    }

    bool sees(Player& player, std::uint32_t networkId)
    {
        return player.replication && player.replication->entity(networkId).has_value();
    }
}

Test(room_stack, two_players_see_each_other)
{
    net::LoopbackNetwork network(net::LoopbackNetwork::Conditions{.loss = 0.05, .latency = 0.03, .jitter = 0.01, .seed = 3});
    server::ServerConfig config;

    config.transport = server::Transport::Loopback;
    config.loopback = &network;
    config.tickRate = 60;
    server::GameServer gameServer(config);

    gameServer.addRoomType<ReplicatedRoom>("arena", server::RoomTypeConfig{.maxPlayers = 2});
    std::thread thread([&gameServer] { gameServer.run(); });

    while (!gameServer.stats().lobbyOpen) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    {
        Player ana(gameServer, network, "Ana");
        Player ben(gameServer, network, "Ben");
        const auto joined = Clock::now() + std::chrono::seconds(20);

        while ((!ana.prediction || !ben.prediction || !ana.prediction->ready() || !ben.prediction->ready()) && Clock::now() < joined) {
            ana.tick();
            ben.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Assert(ana.prediction && ben.prediction && ana.prediction->ready() && ben.prediction->ready(), "both joined and got their entity");
        AssertEq(facts().players, 2, "the room has two players");
        runFor({&ana, &ben}, 0.5);
        Assert(sees(ana, ben.networkId) && sees(ben, ana.networkId), "each one sees the other's entity");
        AssertEq(ana.replication->entityCount(), 2, "two entities on each client");

        // Ana walks right and down, Ben up and left, for two seconds, into the walls
        ana.steer = Steer{1, 1};
        ben.steer = Steer{-1, -1};
        runFor({&ana, &ben}, 2.0);
        ana.steer = Steer{};
        ben.steer = Steer{};
        runFor({&ana, &ben}, 1.5);
        std::map<std::uint32_t, Vec2> truth;

        {
            std::lock_guard lock(facts().mutex);
            truth = facts().positions;
        }
        Assert(distanceBetween(ana.position(), truth[ana.networkId]) < 0.1f, "Ana's predicted position is the server's: (%f, %f) and (%f, %f)", ana.position().x, ana.position().y, truth[ana.networkId].x, truth[ana.networkId].y);
        Assert(distanceBetween(ben.position(), truth[ben.networkId]) < 0.1f, "so is Ben's");
        const auto benOnAna = ana.replication->entity(ben.networkId);

        Assert(benOnAna.has_value() && distanceBetween(ana.world.get<Transform2D>(*benOnAna).position, truth[ben.networkId]) < 0.1f, "Ana sees Ben where he is (interpolated, then still)");
        const auto anaOnBen = ben.replication->entity(ana.networkId);

        Assert(anaOnBen.has_value() && distanceBetween(ben.world.get<Transform2D>(*anaOnBen).position, truth[ana.networkId]) < 0.1f, "and Ben sees Ana");
        Assert(truth[ana.networkId].x > 100.0f, "they did move: Ana is at (%f, %f)", truth[ana.networkId].x, truth[ana.networkId].y);
        AssertEq(ana.replication->stats().malformed, 0, "no snapshot was malformed");
        Assert(ana.prediction->stats().snaps == 0 && ben.prediction->stats().snaps == 0, "nobody was put back by force");
        std::printf("    (Ana: %llu corrections in %llu snapshots; the room sent %llu snapshots)\n",
            static_cast<unsigned long long>(ana.prediction->stats().corrections), static_cast<unsigned long long>(ana.prediction->stats().reconciliations),
            static_cast<unsigned long long>(facts().snapshots));

        // Ben leaves: his entity goes from Ana's world
        ben.matchmaking.disconnect();
        ben.prediction.reset();
        ben.replication.reset();
        runFor({&ana}, 1.0);
        Assert(!sees(ana, ben.networkId), "when Ben leaves, his entity is removed on Ana's client");
        AssertEq(ana.replication->entityCount(), 1, "only her own is left");
    }
    gameServer.stop();
    thread.join();
}
