#pragma once
// The client of Arena: a window on a 3D game that runs on a server, which answers the keys at once
// (prediction). It knows the room only through the network module and the replication: the same scene
// plays against a server on another machine (sockets) and against one in the same process (loopback).

#include "Arena.hpp"
#include "ClientModule.hpp"
#include "Components3D.hpp"
#include "Input.hpp"
#include "Logger.hpp"
#include "Matchmaking.hpp"
#include "Models.hpp"
#include "Net.hpp"
#include "Prediction.hpp"
#include "ReplicationClient.hpp"
#include "Scene3D.hpp"
#include <functional>
#include <kronk3d/render/Skybox.hpp>
#include <memory>
#include <string>

namespace arena
{

    enum class Action : std::uint8_t { Forward, Back, Left, Right, Jump };

    inline void bindDefaults(kuge::InputMap& input)
    {
        using kuge::Key;

        input.declare(Action::Forward, "forward");
        input.declare(Action::Back, "back");
        input.declare(Action::Left, "left");
        input.declare(Action::Right, "right");
        input.declare(Action::Jump, "jump");
        input.bind(Action::Forward, Key::W);
        input.bind(Action::Forward, Key::Up);
        input.bind(Action::Back, Key::S);
        input.bind(Action::Back, Key::Down);
        input.bind(Action::Left, Key::A);
        input.bind(Action::Left, Key::Left);
        input.bind(Action::Right, Key::D);
        input.bind(Action::Right, Key::Right);
        input.bind(Action::Jump, Key::Space);
    }

    //! Where the server is, and who is playing
    struct ClientOptions
    {
        bool                        sockets  = true;                 //!< false: a server of the same process, by name
        std::string                 host     = "127.0.0.1";
        std::uint16_t               port     = 4343;
        std::string                 lobby    = "lobby";              //!< Loopback: the name of its lobby
        kuge::net::LoopbackNetwork* network  = nullptr;              //!< Loopback: null is the process-wide one
        std::string                 name     = "player";
        bool                        rejoin   = true;                 //!< Ask for another game when one ends
        bool                        captureMouse = true;             //!< The mouse turns the view (false for a script)
    };

    //! What the scene saw, for a script or a test to check (filled by the scene, read after the run)
    struct ClientReport
    {
        bool            joined = false;             //!< Was welcomed in a room, at least once
        std::uint32_t   networkId = 0;
        std::size_t     players = 0, coins = 0, crates = 0;                 //!< Entities that exist now
        std::size_t     mostPlayers = 0, mostCoins = 0, mostCrates = 0;     //!< At the most
        std::uint64_t   games = 0;                  //!< Welcomes
        std::uint64_t   gamesEnded = 0;             //!< "Room closed" heard
        int             score = 0;                  //!< Of the player
        kuge::replication::PredictionStats prediction;
        kuge::replication::ReplicationClientStats replication;
    };

    class ArenaScene : public kuge::Scene3D
    {
        public:
            ArenaScene(ClientOptions options, std::shared_ptr<ClientReport> report)
                : m_options(std::move(options)), m_report(std::move(report)), m_registry(makeRegistry()) {}

            void onEnter(void) override;

        private:
            class Work final : public kw::ISystem
            {
                public:
                    explicit Work(std::function<void(kw::World&)> fn) : m_fn(std::move(fn)) {}

                    bool handle(kw::World& world) override { m_fn(world); return true; }

                private:
                    std::function<void(kw::World&)> m_fn;
            };

            void makeStage(void);
            void connect(void);
            void spawned(kw::Entity entity, const kuge::replication::SpawnInfo& info);
            void joined(kuge::net::Endpoint& room, const kuge::net::Welcome& welcome);
            void clearLastGame(void);
            void tick(kw::World& world);
            void frame(kw::World& world);

            ClientOptions                                         m_options;
            std::shared_ptr<ClientReport>                         m_report;
            kuge::replication::ReplicationRegistry                m_registry;
            std::unique_ptr<kuge::net::MatchmakingClient>         m_matchmaking;
            std::unique_ptr<kuge::replication::ReplicationClient> m_replication;
            std::unique_ptr<kuge::replication::Prediction<Steer, kuge::Transform3D>> m_prediction;
            std::shared_ptr<const k3::Model>                      m_coin = coinModel();
            std::shared_ptr<const k3::Model>                      m_crate = crateModel();
            std::vector<kw::Entity>                               m_hud;       //!< One square for each coin taken
            std::uint32_t                                         m_networkId = 0;
            std::uint32_t                                         m_waited = 0;   //!< Ticks since the lobby was lost
    };

    // -- Implementation ------------------------------------------------------------------------------------

    // The level (the same on every machine, so nobody sends it), the sky and the light, and the interface
    inline void ArenaScene::makeStage(void)
    {
        const auto solidModel = [this](std::shared_ptr<const k3::Model> model, Vec3 at) {
            const kw::Entity entity = world().create();
            kuge::Transform3D transform;

            transform.translation = at;
            world().add<kuge::Transform3D>(entity, transform);
            world().add<kuge::Model3D>(entity, kuge::Model3D{std::move(model)});
        };
        constexpr float EDGE = HALF_SIZE + 0.5f;

        solidModel(floorModel(), {0.0f, -0.5f, 0.0f});
        solidModel(wallModel(2.0f * HALF_SIZE + 2.0f, true), {0.0f, 1.5f, -EDGE});
        solidModel(wallModel(2.0f * HALF_SIZE + 2.0f, true), {0.0f, 1.5f, EDGE});
        solidModel(wallModel(2.0f * HALF_SIZE + 2.0f, false), {-EDGE, 1.5f, 0.0f});
        solidModel(wallModel(2.0f * HALF_SIZE + 2.0f, false), {EDGE, 1.5f, 0.0f});

        auto& environment = world().getResource<k3::Environment>();

        environment.skybox = k3::Skybox::gradient({0.04f, 0.12f, 0.35f, 1.0f}, {0.4f, 0.55f, 0.75f, 1.0f}, {0.08f, 0.09f, 0.1f, 1.0f});
        environment.ambient = {0.3f, 0.32f, 0.38f, 1.0f};
        environment.lights.push_back(k3::Light::directional({-0.4f, -1.0f, -0.3f}, {1.0f, 0.95f, 0.85f, 1.0f}, 1.1f));
        // The interface, over the 3D scene: a square for each coin taken (its place is set at each frame)
        for (std::size_t i = 0; i < COIN_COUNT; ++i) {
            const kw::Entity square = world().create();
            kuge::Sprite sprite;

            sprite.size = {12.0f, 12.0f};
            sprite.pivot = {0.0f, 0.0f};
            sprite.tint = kuge::Color{255, 255, 255, 255};
            sprite.layer = 10;
            sprite.visible = false;
            world().add<kuge::Transform2D>(square, kuge::Transform2D{{10.0f + 16.0f * static_cast<float>(i), 10.0f}});
            world().add<kuge::Sprite>(square, sprite);
            m_hud.push_back(square);
        }
    }

    // What the server does not send: how things look
    inline void ArenaScene::spawned(kw::Entity entity, const kuge::replication::SpawnInfo& info)
    {
        switch (info.type) {
            case PLAYER: {
                const bool me = info.owner == m_networkId;
                // Mine is cyan; the others take a colour each (all pure channels, so that a script can find them)
                static const k3::Math::Color colors[4] = {{0, 1, 1, 1}, {1, 0, 1, 1}, {0, 1, 0, 1}, {1, 0.5f, 0, 1}};

                world().add<kuge::Model3D>(entity, kuge::Model3D{playerModel(me ? colors[0] : colors[1 + info.owner % 3])});
                if (me) {
                    kuge::CameraRig rig;

                    rig.mode = kuge::CameraRig::Mode::Orbit;
                    rig.offset = {0.0f, 0.5f, 0.0f};
                    rig.distance = 5.0f;
                    rig.pitch = -0.3f;
                    world().add<kuge::CameraRig>(entity, rig);
                }
                break;
            }
            case COIN:
                world().add<kuge::Model3D>(entity, kuge::Model3D{m_coin});
                break;
            default:
                world().add<kuge::Model3D>(entity, kuge::Model3D{m_crate});
                break;
        }
    }

    // The entities of a finished game: the new ReplicationClient does not know them, so nobody will
    // remove them but us (they would stay on screen, frozen, for good)
    inline void ArenaScene::clearLastGame(void)
    {
        std::vector<kw::Entity> old;

        for (kw::Entity entity : world().view<kuge::replication::Replicated>()) {
            old.push_back(entity);
        }
        for (kw::Entity entity : old) {
            world().remove(entity);
        }
    }

    inline void ArenaScene::joined(kuge::net::Endpoint& room, const kuge::net::Welcome& welcome)
    {
        m_prediction.reset();
        m_replication.reset();
        clearLastGame();
        m_networkId = welcome.networkId;
        m_replication = std::make_unique<kuge::replication::ReplicationClient>(world(), m_registry,
            kuge::replication::ReplicationClientConfig{.tickRate = welcome.tickRate});
        m_replication->setLocalPlayer(welcome.networkId);
        m_replication->predictType(PLAYER);
        for (const auto type : {PLAYER, COIN, CRATE}) {
            m_replication->onSpawn(type, [this](kw::World&, kw::Entity entity, const kuge::replication::SpawnInfo& info) { spawned(entity, info); });
        }
        m_replication->attach(room);

        kuge::replication::PredictionConfig<Steer> config;

        config.build = [](kw::World& w) { buildLevel(w); return buildPlayer(w); };
        config.apply = &steerPlayer;
        config.step = &stepWorld;
        config.dt = TICK_DT;
        config.snapDistance = 3.0f;      // meters
        m_prediction = std::make_unique<kuge::replication::Prediction<Steer, kuge::Transform3D>>(config, world(), m_registry, *m_replication, room);
        m_report->joined = true;
        m_report->networkId = welcome.networkId;
        ++m_report->games;
    }

    inline void ArenaScene::onEnter(void)
    {
        installClient3DSystems();
        kuge::net::installNet(setup());
        makeStage();
        if (m_options.captureMouse) {
            world().getResource<kuge::Ref<kuge::IInputSource>>()->setRelativeMouse(true);
        }
        m_matchmaking = std::make_unique<kuge::net::MatchmakingClient>(world().getResource<kuge::net::Net>());
        m_matchmaking->onJoined([this](kuge::net::Endpoint& room, const kuge::net::Welcome& welcome) { joined(room, welcome); });
        m_matchmaking->onRoomClosed([this](kuge::net::RoomEnd) {
            ++m_report->gamesEnded;
            m_prediction.reset();
            m_replication.reset();           // (its entities stay until the next game, see clearLastGame(): the last picture)
            if (m_options.rejoin) {
                m_matchmaking->join("arena", m_options.name);
            }
        });
        connect();
        addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Work>([this](kw::World& w) { tick(w); }));
        addSystem(kw::Schedule::Frame, kuge::stage::Late, std::make_unique<Work>([this](kw::World& w) { frame(w); }));
    }

    inline void ArenaScene::connect(void)
    {
        if (m_options.sockets) {
            m_matchmaking->connectLobby(kuge::net::Protocol::Tcp, m_options.host, m_options.port);
        } else {
            m_matchmaking->connectLobby(m_options.lobby, m_options.network ? *m_options.network : kuge::net::LoopbackNetwork::global());
        }
        m_matchmaking->join("arena", m_options.name);
    }

    // Each fixed tick: what the keys say, and where the player looks, go to the prediction (and so to the server)
    inline void ArenaScene::tick(kw::World& world)
    {
        const auto& actions = world.getResource<kuge::ActionState>();
        Steer steer;

        steer.forward = static_cast<std::int8_t>((actions.isDown(Action::Forward) ? 1 : 0) - (actions.isDown(Action::Back) ? 1 : 0));
        steer.strafe = static_cast<std::int8_t>((actions.isDown(Action::Right) ? 1 : 0) - (actions.isDown(Action::Left) ? 1 : 0));
        steer.jump = actions.isDown(Action::Jump);
        if (m_prediction) {
            if (const auto body = m_prediction->entity(); body && world.has<kuge::CameraRig>(*body)) {
                steer.yaw = quantizeYaw(world.get<kuge::CameraRig>(*body).yaw);
            }
            m_prediction->tick(steer);
        }
        // No server (yet, or any more): try again every second, so that the clients can be started before the server
        if (m_matchmaking->state() == kuge::net::MatchmakingClient::State::Failed) {
            if (++m_waited >= 60) {
                m_waited = 0;
                Logger::logger().info("arena: no server at {}:{}, trying again", m_options.host, m_options.port);
                connect();
            }
        } else {
            m_waited = 0;
        }
    }

    // Each frame: the other entities are placed (interpolated), and what is shown is looked after
    inline void ArenaScene::frame(kw::World& world)
    {
        const auto frameDt = static_cast<float>(world.getResource<kuge::Time>().frameDt);
        const auto screen = world.getResource<kuge::Ref<kuge::IRenderer2D>>()->outputSize();

        world.getResource<kuge::Camera2D>().position = screen * 0.5f;       // (pixels of the screen)
        if (m_replication) {
            m_replication->update(frameDt);
        }
        ClientReport& report = *m_report;
        std::size_t players = 0, coins = 0, crates = 0;
        int score = 0;

        for (kw::Entity entity : world.view<kuge::replication::Replicated>()) {
            const auto& info = world.get<kuge::replication::Replicated>(entity);

            players += info.type == PLAYER ? 1 : 0;
            coins += info.type == COIN ? 1 : 0;
            crates += info.type == CRATE ? 1 : 0;
            if (info.type == PLAYER && info.owner == m_networkId && world.has<Score>(entity)) {
                score = world.get<Score>(entity).points;
            }
        }
        for (std::size_t i = 0; i < m_hud.size(); ++i) {
            world.get<kuge::Sprite>(m_hud[i]).visible = static_cast<int>(i) < score;
        }
        report.players = players;
        report.coins = coins;
        report.crates = crates;
        report.score = score;
        report.mostPlayers = std::max(report.mostPlayers, players);
        report.mostCoins = std::max(report.mostCoins, coins);
        report.mostCrates = std::max(report.mostCrates, crates);
        if (m_prediction) {
            report.prediction = m_prediction->stats();
        }
        if (m_replication) {
            report.replication = m_replication->stats();
        }
    }

}
