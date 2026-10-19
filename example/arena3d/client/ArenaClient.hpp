#pragma once
// The client of Arena: a window on a 3D game that runs on a server, which answers the keys at once
// (prediction). It knows the room only through the network module and the replication: the same scene
// plays against a server on another machine (sockets) and against one in the same process (loopback).

#include "Animator3D.hpp"
#include "Arena.hpp"
#include "AssetManager.hpp"
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
#include <cmath>
#include <filesystem>
#include <functional>
#include <kronk3d/render/Skybox.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>

#ifndef ARENA_ASSETS_DIR
#define ARENA_ASSETS_DIR "assets"
#endif

namespace arena
{

    enum class Action : std::uint8_t { Forward, Back, Left, Right, Jump, Fire, View, Release };

    inline void bindDefaults(kuge::InputMap& input)
    {
        using kuge::Key;

        input.declare(Action::Forward, "forward");
        input.declare(Action::Back, "back");
        input.declare(Action::Left, "left");
        input.declare(Action::Right, "right");
        input.declare(Action::Jump, "jump");
        input.declare(Action::Fire, "fire");
        input.declare(Action::View, "view");
        input.declare(Action::Release, "release");
        input.bind(Action::Forward, Key::W);
        input.bind(Action::Forward, Key::Up);
        input.bind(Action::Back, Key::S);
        input.bind(Action::Back, Key::Down);
        input.bind(Action::Left, Key::A);
        input.bind(Action::Left, Key::Left);
        input.bind(Action::Right, Key::D);
        input.bind(Action::Right, Key::Right);
        input.bind(Action::Jump, Key::Space);
        input.bind(Action::Fire, kuge::MouseButton::Left);
        input.bind(Action::Fire, Key::F);
        input.bind(Action::View, Key::V);
        input.bind(Action::Release, Key::Escape);
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
        float                       sensitivity = 0.075f;            //!< Degrees of the view for a pixel of the mouse
        bool                        firstPerson = true;              //!< The view of the eyes (V changes it)
        double                      daySeconds = 150.0;              //!< A whole day, from dawn to dawn
        std::filesystem::path       assets   = ARENA_ASSETS_DIR;     //!< The models and textures
    };

    //! What the scene saw, for a script or a test to check (filled by the scene, read after the run)
    struct ClientReport
    {
        bool            joined = false;             //!< Was welcomed in a room, at least once
        std::uint32_t   networkId = 0;
        std::size_t     players = 0, crates = 0, bullets = 0, corpses = 0;                //!< Entities that exist now
        std::size_t     mostPlayers = 0, mostBullets = 0, mostCorpses = 0;                //!< At the most
        std::uint64_t   games = 0;                  //!< Welcomes
        std::uint64_t   gamesEnded = 0;             //!< "Room closed" heard
        int             health = 0, frags = 0;      //!< Of the player
        std::set<std::string> clips;                //!< The clips that the player's own character played
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

            void loadAssets(void);
            void capture(bool on);
            void applySky(double seconds);
            void makeStage(void);
            void connect(void);
            void spawned(kw::Entity entity, const kuge::replication::SpawnInfo& info);
            void joined(kuge::net::Endpoint& room, const kuge::net::Welcome& welcome);
            void clearLastGame(void);
            void tick(kw::World& world);
            void frame(kw::World& world);
            kw::Entity square(kuge::Color color, kuge::Vec2 size, int layer);

            ClientOptions                                         m_options;
            std::shared_ptr<ClientReport>                         m_report;
            kuge::replication::ReplicationRegistry                m_registry;
            std::unique_ptr<kuge::net::MatchmakingClient>         m_matchmaking;
            std::unique_ptr<kuge::replication::ReplicationClient> m_replication;
            std::unique_ptr<kuge::replication::Prediction<Steer, kuge::Transform3D>> m_prediction;
            std::shared_ptr<const k3::AnimatedModel>              m_player;     //!< As the file has it
            std::shared_ptr<const k3::AnimatedModel>              m_painted[4];
            std::shared_ptr<const k3::Model>                      m_crate;
            std::shared_ptr<const k3::Model>                      m_bullet;
            kw::Entity                                            m_gun{};      //!< What the player holds
            float                                                 m_kick = 0.0f;
            std::uint8_t                                          m_shots = 0;  //!< The serial of the last shot seen
            Proxies                                               m_proxies;    //!< In the predicted world: what is near the player
            bool                                                  m_captured = false;
            double                                                m_seconds = 0.0;
            kw::Entity                                            m_health{};   //!< The bar of the HUD
            std::vector<kw::Entity>                               m_crosshair;
            std::vector<kw::Entity>                               m_fragSquares;   //!< FRAG_LIMIT squares for each of four players
            std::uint32_t                                         m_networkId = 0;
            std::uint32_t                                         m_waited = 0;   //!< Ticks since the lobby was lost
            bool                                                  m_firstPerson = true;
    };

    // -- Implementation ------------------------------------------------------------------------------------

    inline kw::Entity ArenaScene::square(kuge::Color color, kuge::Vec2 size, int layer)
    {
        const kw::Entity entity = world().create();
        kuge::Sprite sprite;

        sprite.size = size;
        sprite.pivot = {0.0f, 0.0f};
        sprite.tint = color;
        sprite.layer = layer;
        sprite.visible = false;
        world().add<kuge::Transform2D>(entity, kuge::Transform2D{});
        world().add<kuge::Sprite>(entity, sprite);
        return entity;
    }

    // The files: the character (glTF, with its clips), the crate (OBJ, with its texture), the textures of the level
    inline void ArenaScene::loadAssets(void)
    {
        auto& objects = *world().getResource<kuge::Ref<kuge::AssetManager<k3::Model>>>();
        auto& characters = *world().getResource<kuge::Ref<kuge::AssetManager<k3::AnimatedModel>>>();

        m_player = characters.load(m_options.assets / "robot.glb");
        for (std::uint32_t i = 0; i < 4; ++i) {
            m_painted[i] = makeRobot(*m_player, teamColor(i + 1));
        }
        m_crate = objects.load(m_options.assets / "crate.obj");
        m_bullet = bulletModel();
    }

    // The level (the same on every machine, so nobody sends it), the sky and the light, and the interface
    inline void ArenaScene::makeStage(void)
    {
        const LevelTextures textures(m_options.assets);

        for (const Piece& piece : level()) {
            const kw::Entity entity = world().create();
            kuge::Transform3D transform;

            transform.translation = piece.center;
            transform.rotation = piece.rotation;
            world().add<kuge::Transform3D>(entity, transform);
            world().add<kuge::Model3D>(entity, kuge::Model3D{textures.modelOf(piece)});
        }
        auto& environment = world().getResource<k3::Environment>();

        // The sky is one picture, dimmed and tinted along the day (see applySky()); the sun and the moon are the two lights
        environment.skybox = k3::Skybox::gradient({0.04f, 0.12f, 0.35f, 1.0f}, {0.45f, 0.6f, 0.8f, 1.0f}, {0.08f, 0.09f, 0.1f, 1.0f});
        environment.ambientGround = k3::Math::Color{0.12f, 0.11f, 0.1f, 1.0f};
        environment.lights.push_back(k3::Light::directional({-0.4f, -1.0f, -0.3f}));
        environment.lights.push_back(k3::Light::directional({0.4f, -1.0f, 0.3f}));
        environment.fog.density = 0.022f;          // a third of the light left at 50 m
        environment.fog.heightFalloff = 0.0f;
        applySky(0.0);
        m_gun = world().create();
        world().add<kuge::ViewModel3D>(m_gun, kuge::ViewModel3D{gunModel()});
        // The interface, over the 3D scene (its places are set at each frame): a crosshair, the health, and the
        // frags of each player, a square for each, in the colour of its shirt
        for (int i = 0; i < 4; ++i) {
            m_crosshair.push_back(square(kuge::Color{255, 255, 255, 255}, {2.0f, 2.0f}, 10));
        }
        m_health = square(kuge::Color{0, 255, 0, 255}, {200.0f, 8.0f}, 10);
        for (std::uint32_t player = 1; player <= 4; ++player) {
            const k3::Math::Color color = teamColor(player);

            for (int i = 0; i < FRAG_LIMIT; ++i) {
                m_fragSquares.push_back(square(kuge::Color{static_cast<std::uint8_t>(color.r * 255), static_cast<std::uint8_t>(color.g * 255),
                    static_cast<std::uint8_t>(color.b * 255), 255}, {10.0f, 10.0f}, 10));
            }
        }
    }

    // The mouse is the view's (taken, hidden, kept in the window) or the desktop's (Esc gives it back, a click takes it)
    inline void ArenaScene::capture(bool on)
    {
        m_captured = on;
        world().getResource<kuge::Ref<kuge::IInputSource>>()->setRelativeMouse(on);
        for (kw::Entity entity : world().view<kuge::CameraRig>()) {
            world().get<kuge::CameraRig>(entity).look = on;
        }
    }

    // The light of a moment of the day (seconds since the match began, the same for every player: it is counted in
    // ticks of the server). The sun goes over the arena from east to west, the moon is opposite; the sky is the same
    // picture all along, dimmed and tinted; the fog is the colour of the horizon.
    inline void ArenaScene::applySky(double seconds)
    {
        using Color = k3::Math::Color;
        const auto mix = [](const Color& a, const Color& b, float k) {
            return Color{a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, 1.0f};
        };
        const auto smooth = [](float low, float high, float x) {
            const float k = std::clamp((x - low) / (high - low), 0.0f, 1.0f);

            return k * k * (3.0f - 2.0f * k);
        };
        const double phase = std::fmod(seconds / m_options.daySeconds + 0.28, 1.0);     // (the match begins in the morning)
        const float turn = static_cast<float>(phase * 6.2831853);
        const float height = -std::cos(turn);                                           // the sun: -1 at midnight, 1 at noon
        const Vec3 sun{std::sin(turn) * 0.9f, height, 0.35f};
        const float length = Vec3::length(sun);
        const float day = smooth(-0.12f, 0.3f, height);
        const float dusk = std::clamp(1.0f - std::fabs(height) / 0.3f, 0.0f, 1.0f);
        auto& environment = world().getResource<k3::Environment>();

        environment.lights[0].direction = {-sun.x / length, -sun.y / length, -sun.z / length};
        environment.lights[0].color = mix(Color{1.0f, 0.5f, 0.22f, 1.0f}, Color{1.0f, 0.96f, 0.88f, 1.0f}, std::clamp(height / 0.5f, 0.0f, 1.0f));
        environment.lights[0].intensity = 1.2f * day;
        environment.lights[1].direction = {sun.x / length, sun.y / length, sun.z / length};
        environment.lights[1].color = Color{0.55f, 0.65f, 1.0f, 1.0f};
        environment.lights[1].intensity = 0.3f * (1.0f - day);
        environment.ambient = mix(Color{0.03f, 0.035f, 0.075f, 1.0f}, Color{0.34f, 0.36f, 0.42f, 1.0f}, day);
        environment.ambientGround = mix(Color{0.012f, 0.012f, 0.018f, 1.0f}, Color{0.12f, 0.11f, 0.1f, 1.0f}, day);
        environment.skybox.tint = mix(mix(Color{0.035f, 0.045f, 0.11f, 1.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, day), Color{1.0f, 0.6f, 0.4f, 1.0f}, dusk * 0.6f);
        environment.fog.color = mix(mix(Color{0.02f, 0.03f, 0.07f, 1.0f}, Color{0.5f, 0.62f, 0.78f, 1.0f}, day), Color{0.85f, 0.45f, 0.3f, 1.0f}, dusk * 0.5f);
    }

    // What the server does not send: how things look
    inline void ArenaScene::spawned(kw::Entity entity, const kuge::replication::SpawnInfo& info)
    {
        switch (info.type) {
            case PLAYER:
            case CORPSE: {
                const auto& model = m_painted[(info.owner + 3) % 4];

                world().add<kuge::Animator3D>(entity, kuge::Animator3D::of(model, info.type == CORPSE ? "death" : "idle"));
                if (info.type == PLAYER && info.owner == m_networkId) {
                    kuge::CameraRig rig;
                    const auto& rotation = world().get<kuge::Transform3D>(entity).rotation;

                    rig.mode = m_firstPerson ? kuge::CameraRig::Mode::FirstPerson : kuge::CameraRig::Mode::Orbit;
                    rig.offset = {0.0f, m_firstPerson ? EYE_HEIGHT : 0.7f, 0.0f};
                    rig.distance = 4.0f;
                    rig.yaw = -2.0f * std::atan2(rotation.y, rotation.w);      // (the way the body faces)
                    rig.pitch = m_firstPerson ? 0.0f : -0.15f;
                    rig.sensitivity = m_options.sensitivity * 3.14159265f / 180.0f;
                    rig.look = m_captured;
                    // From behind, the camera stops at the walls of the level (the predicted world has them)
                    rig.obstacle = [this](Vec3 from, Vec3 way, float most) {
                        if (!m_prediction || !m_prediction->ready()) {
                            return -1.0f;
                        }
                        const auto hit = m_prediction->privateWorld().getResource<kuge::Physics3D>().raycast(from, way, most, SCENERY);

                        return hit ? hit->distance : -1.0f;
                    };
                    world().add<kuge::CameraRig>(entity, rig);
                }
                break;
            }
            case BULLET:
                world().add<kuge::Model3D>(entity, kuge::Model3D{m_bullet});
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
        for (const auto type : {PLAYER, CRATE, BULLET, CORPSE}) {
            m_replication->onSpawn(type, [this](kw::World&, kw::Entity entity, const kuge::replication::SpawnInfo& info) { spawned(entity, info); });
        }
        m_replication->attach(room);

        kuge::replication::PredictionConfig<Steer> config;

        config.build = [](kw::World& w) { buildLevel(w); return buildPlayer(w); };
        config.apply = &steerPlayer;
        config.step = [this](kw::World& predicted, double dt) {
            syncProxies(m_proxies, predicted, world(), m_networkId);     // (what is near the player is in the world that predicts it)
            stepWorld(predicted, dt);
        };
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
        loadAssets();
        makeStage();
        m_firstPerson = m_options.firstPerson;
        capture(m_options.captureMouse);
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
        // (a tap shorter than a tick is still a jump: ActionState keeps what was pressed since the last tick)
        steer.jump = actions.isDown(Action::Jump) || actions.wasPressed(Action::Jump);
        // Esc gives the mouse back; a click takes it again (and that click does not fire)
        bool taken = false;

        if (actions.wasPressed(Action::Release) && m_captured) {
            capture(false);
        } else if (!m_captured && actions.wasPressed(Action::Fire)) {
            capture(true);
            taken = true;
        }
        steer.fire = m_captured && !taken && (actions.isDown(Action::Fire) || actions.wasPressed(Action::Fire));
        if (m_prediction) {
            if (const auto body = m_prediction->entity(); body && world.has<kuge::CameraRig>(*body)) {
                auto& rig = world.get<kuge::CameraRig>(*body);

                steer.yaw = quantize(rig.yaw);
                steer.pitch = quantize(rig.pitch);
                if (actions.wasPressed(Action::View)) {     // V: from the eyes, or from behind
                    m_firstPerson = !m_firstPerson;
                    rig.mode = m_firstPerson ? kuge::CameraRig::Mode::FirstPerson : kuge::CameraRig::Mode::Orbit;
                    rig.offset.y = m_firstPerson ? EYE_HEIGHT : 0.7f;
                }
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

    // Each frame: the other entities are placed (interpolated), and the interface is set to what is happening
    inline void ArenaScene::frame(kw::World& world)
    {
        const auto frameDt = static_cast<float>(world.getResource<kuge::Time>().frameDt);
        const auto screen = world.getResource<kuge::Ref<kuge::IRenderer2D>>()->outputSize();

        world.getResource<kuge::Camera2D>().position = screen * 0.5f;       // (pixels of the screen)
        if (m_replication) {
            m_replication->update(frameDt);
        }
        ClientReport& report = *m_report;
        std::size_t players = 0, crates = 0, bullets = 0, corpses = 0;
        int frags[4] = {0, 0, 0, 0};
        int health = 0;
        bool mine = false;

        for (kw::Entity entity : world.view<kuge::replication::Replicated>()) {
            const auto& info = world.get<kuge::replication::Replicated>(entity);

            players += info.type == PLAYER ? 1 : 0;
            crates += info.type == CRATE ? 1 : 0;
            bullets += info.type == BULLET ? 1 : 0;
            corpses += info.type == CORPSE ? 1 : 0;
            if (info.type != PLAYER) {
                continue;
            }
            if (world.has<Score>(entity)) {
                frags[(info.owner + 3) % 4] = world.get<Score>(entity).frags;
            }
            if (info.owner == m_networkId) {
                mine = true;
                health = world.has<Health>(entity) ? world.get<Health>(entity).points : 0;
                report.frags = world.has<Score>(entity) ? world.get<Score>(entity).frags : report.frags;
                if (world.has<kuge::AnimationState3D>(entity)) {
                    const auto& state = world.get<kuge::AnimationState3D>(entity);

                    if (state.serial != m_shots && state.clip.rfind("shooting", 0) == 0) {
                        m_kick = 0.07f;
                    }
                    m_shots = state.serial;
                }
                if (world.has<kuge::Animator3D>(entity)) {
                    auto& animation = world.get<kuge::Animator3D>(entity);

                    animation.visible = !m_firstPerson;      // (from its eyes, a player does not see its own body)
                    report.clips.insert(std::string(animation.animator.clip()));
                }
            }
        }
        // The gun is held in first person, and kicks back when its player shoots
        auto& held = world.get<kuge::ViewModel3D>(m_gun);

        held.visible = mine && m_firstPerson;
        m_kick = std::max(0.0f, m_kick - frameDt * 0.7f);
        held.kick = m_kick;
        // The day goes on (by the clock of the server once there is one)
        m_seconds = m_replication ? m_replication->renderTick() / 60.0 : m_seconds + frameDt;
        applySky(m_seconds);
        // The interface
        for (std::size_t i = 0; i < m_crosshair.size(); ++i) {
            // Four ticks around the middle of the screen
            const kuge::Vec2 middle = screen * 0.5f;
            const kuge::Vec2 places[4] = {{middle.x - 1.0f, middle.y - 10.0f}, {middle.x - 1.0f, middle.y + 6.0f},
                {middle.x - 10.0f, middle.y - 1.0f}, {middle.x + 6.0f, middle.y - 1.0f}};
            auto& sprite = world.get<kuge::Sprite>(m_crosshair[i]);

            world.get<kuge::Transform2D>(m_crosshair[i]).position = places[i];
            sprite.size = i < 2 ? kuge::Vec2{2.0f, 4.0f} : kuge::Vec2{4.0f, 2.0f};
            sprite.visible = mine;
        }
        auto& bar = world.get<kuge::Sprite>(m_health);

        world.get<kuge::Transform2D>(m_health).position = {10.0f, screen.y - 20.0f};
        bar.size = {2.0f * static_cast<float>(health), 8.0f};
        bar.tint = health > HEALTH / 2 ? kuge::Color{0, 255, 0, 255} : kuge::Color{255, 0, 0, 255};
        bar.visible = mine && health > 0;
        for (std::size_t i = 0; i < m_fragSquares.size(); ++i) {
            const std::size_t player = i / FRAG_LIMIT;
            const int index = static_cast<int>(i % FRAG_LIMIT);

            world.get<kuge::Transform2D>(m_fragSquares[i]).position = {10.0f + 14.0f * static_cast<float>(index), 10.0f + 14.0f * static_cast<float>(player)};
            world.get<kuge::Sprite>(m_fragSquares[i]).visible = index < frags[player];
        }
        report.players = players;
        report.crates = crates;
        report.bullets = bullets;
        report.corpses = corpses;
        report.health = health;
        report.mostPlayers = std::max(report.mostPlayers, players);
        report.mostBullets = std::max(report.mostBullets, bullets);
        report.mostCorpses = std::max(report.mostCorpses, corpses);
        if (m_prediction) {
            report.prediction = m_prediction->stats();
        }
        if (m_replication) {
            report.replication = m_replication->stats();
        }
    }

}
