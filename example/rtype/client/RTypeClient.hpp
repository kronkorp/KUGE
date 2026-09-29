#pragma once

// The client of R-Type: a window that draws what the room says, and answers the keys at once (prediction).
// It knows the room only through the network module and the replication: the same scene plays against a
// server on another machine (sockets) and against one in the same process (loopback).

#include "ClientModule.hpp"
#include "ClientScene.hpp"
#include "Input.hpp"
#include "Logger.hpp"
#include "Matchmaking.hpp"
#include "Net.hpp"
#include "Prediction.hpp"
#include "RType.hpp"
#include "ReplicationClient.hpp"
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rtype
{

    enum class Action : std::uint8_t { Left, Right, Up, Down, Fire };

    inline void bindDefaults(kuge::InputMap& input)
    {
        using kuge::Key;

        input.declare(Action::Left, "left");
        input.declare(Action::Right, "right");
        input.declare(Action::Up, "up");
        input.declare(Action::Down, "down");
        input.declare(Action::Fire, "fire");
        input.bind(Action::Left, Key::Left);
        input.bind(Action::Left, Key::A);
        input.bind(Action::Right, Key::Right);
        input.bind(Action::Right, Key::D);
        input.bind(Action::Up, Key::Up);
        input.bind(Action::Up, Key::W);
        input.bind(Action::Down, Key::Down);
        input.bind(Action::Down, Key::S);
        input.bind(Action::Fire, Key::Space);
    }

    //! Where the server is, and who is playing
    struct ClientOptions
    {
        bool                        sockets  = true;                 //!< false: a server of the same process, by name
        std::string                 host     = "127.0.0.1";
        std::uint16_t               port     = 4242;
        std::string                 lobby    = "lobby";              //!< Loopback: the name of its lobby
        kuge::net::LoopbackNetwork* network  = nullptr;              //!< Loopback: null is the process-wide one
        std::string                 name     = "pilot";
        bool                        rejoin   = true;                 //!< Ask for another game when one ends
    };

    //! What the scene saw, for a script or a test to check (filled by the scene, read after the run)
    struct ClientReport
    {
        bool            connected = false;          //!< Reached the lobby
        bool            joined = false;             //!< Was welcomed in a room, at least once
        std::uint32_t   networkId = 0;
        std::uint32_t   roomId = 0;                 //!< Of the room it is in (or was in)
        std::size_t     ships = 0, bullets = 0, enemies = 0;                 //!< Entities that exist now
        std::size_t     mostShips = 0, mostBullets = 0, mostEnemies = 0;    //!< At the most
        std::uint64_t   games = 0;                  //!< Welcomes
        std::uint64_t   gamesEnded = 0;             //!< "Room closed" heard
        int             health = 0, score = 0;      //!< Of the player's ship
        kuge::net::MatchmakingClient::State state = kuge::net::MatchmakingClient::State::Idle;
        kuge::replication::PredictionStats prediction;
        kuge::replication::ReplicationClientStats replication;
        kuge::Vec2      shipAt{};                   //!< Where the player's ship is drawn
        std::uint64_t   sprites = 0;                //!< Sprites in the world now
    };

    class RTypeScene : public kuge::ClientScene
    {
        public:
            RTypeScene(ClientOptions options, std::shared_ptr<ClientReport> report)
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

            struct Star { float speed; };

            void makeStars(void);
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
            std::unique_ptr<kuge::replication::Prediction<Steer>> m_prediction;
            std::uint32_t                                         m_networkId = 0;
            std::uint32_t                                         m_waited = 0;   //!< Ticks since the lobby was lost
    };

    // -- Implementation ------------------------------------------------------------------------------------
    inline kuge::Color slotColor(std::uint8_t slot)
    {
        static const kuge::Color colors[4] = {{90, 200, 255, 255}, {120, 230, 110, 255}, {255, 170, 60, 255}, {230, 110, 220, 255}};

        return colors[slot % 4];
    }

    inline void RTypeScene::makeStars(void)
    {
        std::mt19937 rng(7);

        for (int i = 0; i < 70; ++i) {
            const kw::Entity star = world().create();
            kuge::Sprite sprite;
            const float speed = 20.0f + static_cast<float>(rng() % 80);

            sprite.size = {speed > 60.0f ? 2.0f : 1.0f, speed > 60.0f ? 2.0f : 1.0f};
            sprite.tint = kuge::Color{static_cast<std::uint8_t>(90 + speed), static_cast<std::uint8_t>(90 + speed), static_cast<std::uint8_t>(120 + speed / 2), 255};
            sprite.layer = -10;
            world().add<kuge::Transform2D>(star, kuge::Transform2D{{static_cast<float>(rng() % 640), static_cast<float>(rng() % 360)}});
            world().add<kuge::Sprite>(star, sprite);
            world().add<Star>(star, Star{speed});
        }
    }

    // What the server does not send: how things look
    inline void RTypeScene::spawned(kw::Entity entity, const kuge::replication::SpawnInfo& info)
    {
        kuge::Sprite sprite;

        switch (info.type) {
            case SHIP:
                sprite.size = SHIP_SIZE;
                sprite.tint = info.owner == m_networkId ? kuge::Color{255, 255, 255, 255} : slotColor(world().get<Slot>(entity).color);
                sprite.layer = 3;
                break;
            case BULLET:
                sprite.size = BULLET_SIZE;
                sprite.tint = kuge::Color{255, 240, 120, 255};
                sprite.layer = 2;
                break;
            default:
                sprite.size = ENEMY_SIZE;
                sprite.tint = kuge::Color{230, 70, 70, 255};
                sprite.layer = 1;
                break;
        }
        world().add<kuge::Sprite>(entity, sprite);
    }

    // The entities of a finished game: the new ReplicationClient does not know them, so nobody will
    // remove them but us (they would stay on screen, frozen, for good)
    inline void RTypeScene::clearLastGame(void)
    {
        std::vector<kw::Entity> old;
        auto view = world().view<kuge::replication::Replicated>();

        for (kw::Entity entity : view) {
            old.push_back(entity);
        }
        for (kw::Entity entity : old) {
            world().remove(entity);
        }
    }

    inline void RTypeScene::joined(kuge::net::Endpoint& room, const kuge::net::Welcome& welcome)
    {
        m_prediction.reset();
        m_replication.reset();
        clearLastGame();
        m_networkId = welcome.networkId;
        m_replication = std::make_unique<kuge::replication::ReplicationClient>(world(), m_registry,
            kuge::replication::ReplicationClientConfig{.tickRate = welcome.tickRate});
        m_replication->setLocalPlayer(welcome.networkId);
        m_replication->predictType(SHIP);
        for (const auto type : {SHIP, BULLET, ENEMY}) {
            m_replication->onSpawn(type, [this](kw::World&, kw::Entity entity, const kuge::replication::SpawnInfo& info) { spawned(entity, info); });
        }
        m_replication->attach(room);
        kuge::replication::PredictionConfig<Steer> config;

        config.build = [](kw::World& w) { buildArena(w); return buildShip(w); };
        config.apply = &steerShip;
        config.step = &stepArena;
        config.dt = TICK_DT;
        m_prediction = std::make_unique<kuge::replication::Prediction<Steer>>(config, world(), m_registry, *m_replication, room);
        m_report->joined = true;
        m_report->networkId = welcome.networkId;
        m_report->roomId = welcome.roomId;
        ++m_report->games;
    }

    inline void RTypeScene::onEnter(void)
    {
        installClientSystems();
        kuge::net::installNet(setup());
        makeStars();
        m_matchmaking = std::make_unique<kuge::net::MatchmakingClient>(world().getResource<kuge::net::Net>());
        m_matchmaking->onJoined([this](kuge::net::Endpoint& room, const kuge::net::Welcome& welcome) { joined(room, welcome); });
        m_matchmaking->onRoomClosed([this](kuge::net::RoomEnd) {
            ++m_report->gamesEnded;
            m_prediction.reset();
            m_replication.reset();           // (its entities stay until the next game, see clearLastGame(): the last picture)
            if (m_options.rejoin) {
                m_matchmaking->join("rtype", m_options.name);
            }
        });
        connect();
        addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Work>([this](kw::World& w) { tick(w); }));
        addSystem(kw::Schedule::Frame, kuge::stage::Late, std::make_unique<Work>([this](kw::World& w) { frame(w); }));
    }

    inline void RTypeScene::connect(void)
    {
        if (m_options.sockets) {
            m_matchmaking->connectLobby(kuge::net::Protocol::Tcp, m_options.host, m_options.port);
        } else {
            m_matchmaking->connectLobby(m_options.lobby, m_options.network ? *m_options.network : kuge::net::LoopbackNetwork::global());
        }
        m_matchmaking->join("rtype", m_options.name);
    }

    // Each fixed tick: what the keys say goes to the prediction (and so to the server)
    inline void RTypeScene::tick(kw::World& world)
    {
        const auto& actions = world.getResource<kuge::ActionState>();
        Steer steer;

        steer.dx = static_cast<std::int8_t>((actions.isDown(Action::Right) ? 1 : 0) - (actions.isDown(Action::Left) ? 1 : 0));
        steer.dy = static_cast<std::int8_t>((actions.isDown(Action::Down) ? 1 : 0) - (actions.isDown(Action::Up) ? 1 : 0));
        steer.fire = actions.isDown(Action::Fire);
        if (m_prediction) {
            m_prediction->tick(steer);
        }
        // No server (yet, or any more): try again every second, so that the clients can be started before the server
        if (m_matchmaking->state() == kuge::net::MatchmakingClient::State::Failed) {
            if (++m_waited >= 60) {
                m_waited = 0;
                Logger::logger().info("rtype: no server at {}:{}, trying again", m_options.host, m_options.port);
                connect();
            }
        } else {
            m_waited = 0;
        }
    }

    // Each frame: the other entities are placed (interpolated), and what is drawn is looked after
    inline void RTypeScene::frame(kw::World& world)
    {
        const auto frameDt = static_cast<float>(world.getResource<kuge::Time>().frameDt);
        const auto screen = world.getResource<kuge::Ref<kuge::IRenderer2D>>()->outputSize();
        auto& camera = world.getResource<kuge::Camera2D>();

        camera.position = {ARENA_W / 2, ARENA_H / 2};
        camera.zoom = std::min(screen.x / ARENA_W, screen.y / ARENA_H);
        if (m_replication) {
            m_replication->update(frameDt);
        }
        for (kw::Entity star : std::vector<kw::Entity>([&] {
                 std::vector<kw::Entity> stars;
                 auto view = world.view<Star>();

                 for (kw::Entity e : view) { stars.push_back(e); }
                 return stars;
             }())) {
            auto& position = world.get<kuge::Transform2D>(star).position;

            position.x -= world.get<Star>(star).speed * frameDt;
            if (position.x < 0.0f) {
                position.x += ARENA_W;
            }
        }
        // What the report says
        ClientReport& report = *m_report;
        std::size_t ships = 0, bullets = 0, enemies = 0;
        auto view = world.view<kuge::replication::Replicated, kuge::Sprite>();

        report.sprites = 0;
        for (kw::Entity entity : view) {
            const auto& info = world.get<kuge::replication::Replicated>(entity);

            ships += info.type == SHIP ? 1 : 0;
            bullets += info.type == BULLET ? 1 : 0;
            enemies += info.type == ENEMY ? 1 : 0;
            ++report.sprites;
            if (info.type == SHIP) {
                const float hp = world.has<Health>(entity) ? static_cast<float>(world.get<Health>(entity).points) : 1.0f;
                auto& sprite = world.get<kuge::Sprite>(entity);
                const auto base = info.owner == m_networkId ? kuge::Color{255, 255, 255, 255} : slotColor(world.has<Slot>(entity) ? world.get<Slot>(entity).color : 0);
                const float shade = 0.35f + 0.65f * std::min(1.0f, hp / static_cast<float>(settings().shipHealth));

                sprite.tint = kuge::Color{static_cast<std::uint8_t>(base.r * shade), static_cast<std::uint8_t>(base.g * shade), static_cast<std::uint8_t>(base.b * shade), 255};
                if (info.owner == m_networkId) {
                    report.health = static_cast<int>(hp);
                    report.score = world.has<Score>(entity) ? world.get<Score>(entity).points : 0;
                    report.shipAt = world.get<kuge::Transform2D>(entity).position;
                }
            }
        }
        report.ships = ships;
        report.bullets = bullets;
        report.enemies = enemies;
        report.mostShips = std::max(report.mostShips, ships);
        report.mostBullets = std::max(report.mostBullets, bullets);
        report.mostEnemies = std::max(report.mostEnemies, enemies);
        report.state = m_matchmaking->state();
        report.connected = report.connected || m_matchmaking->state() != kuge::net::MatchmakingClient::State::ConnectingLobby;
        if (m_prediction) {
            report.prediction = m_prediction->stats();
        }
        if (m_replication) {
            report.replication = m_replication->stats();
        }
    }

}
