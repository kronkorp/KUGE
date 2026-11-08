#pragma once

// Catch: things fall, you catch them with a paddle. It is a game of 200 lines made to be modded:
//
//   - what a mod can change WITHOUT code: data/catch.json (how fast you are, how often something falls). A mod's file replaces the game's.
//   - what a mod can add WITH code: scripts/*.lua, in a sandbox. A script sees only what this file describes to the ComponentRegistry:
//     `Transform2D` (kuge::describeCore) and `Faller`. It cannot touch the paddle, the score or the window.
//
// Read the mod in mods/golden to see both.

#include "AssetRoots.hpp"
#include "ClientModule.hpp"
#include "ClientScene.hpp"
#include "Engine.hpp"
#include "Json.hpp"
#include "Logger.hpp"
#include "Mods.hpp"
#include "Random.hpp"
#include "ScriptSystem.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace catchgame
{

    enum class Action : std::uint8_t { Left, Right, Exit };

    inline void bindDefaults(kuge::InputMap& input)
    {
        input.declare(Action::Left, "left");
        input.declare(Action::Right, "right");
        input.declare(Action::Exit, "exit");
        input.bind(Action::Left, kuge::Key::Left);
        input.bind(Action::Left, kuge::Key::A);
        input.bind(Action::Right, kuge::Key::Right);
        input.bind(Action::Right, kuge::Key::D);
        input.bind(Action::Exit, kuge::Key::Escape);
    }

    // The field: the origin is its center, y points down
    constexpr float FIELD_W = 640.0f;
    constexpr float FIELD_H = 480.0f;
    constexpr float PLAYER_W = 90.0f;
    constexpr float PLAYER_H = 16.0f;
    constexpr float PLAYER_Y = 210.0f;

    // -- What makes the game ------------------------------------------------------------------
    struct Player {};
    //! Something that falls. Described to the registry: a script can make one, read it and change it
    struct Faller
    {
        float speed = 120.0f;   //!< Pixels per second
        int   value = 1;        //!< Points when caught. A value above 1 is drawn gold
    };

    //! A resource of the World
    struct Score { int points = 0; };

    //! What data/catch.json says (a key that is not there keeps its default)
    struct Settings
    {
        float playerSpeed = 300.0f;
        float spawnEvery = 1.0f;     //!< Seconds between two things that fall

        static Settings load(const kuge::AssetRoots& roots)
        {
            Settings settings;
            const kuge::Json json = kuge::Json::load(roots.resolve("data/catch.json"));   // the highest root that has it: a mod's, else the game's

            if (const kuge::Json* value = json.find("playerSpeed")) {
                settings.playerSpeed = static_cast<float>(value->number());
            }
            if (const kuge::Json* value = json.find("spawnEvery")) {
                settings.spawnEvery = static_cast<float>(value->number());
            }
            return settings;
        }
    };

    //! What the scene needs from main(): the settings, and the scripts of the mods (none if there is no mod with a script)
    struct Setup
    {
        Settings                                settings;
        std::shared_ptr<kuge::ComponentRegistry> registry;   // the engine reads it: it lives as long as the engine
        std::shared_ptr<kuge::ScriptEngine>     scripts;
    };

    //! Puts the game and its mods together. @throw kuge::ModError, kuge::JsonError, kuge::ScriptError: each names the file (and the line)
    inline Setup makeSetup(const std::filesystem::path& gameFolder, const kuge::Mods& mods)
    {
        auto log = Logger::logger().category("modding");
        Setup setup;
        kuge::AssetRoots roots(gameFolder);

        mods.apply(roots);
        for (const auto& mod : mods.ordered()) {
            log.info("mod {} {}", mod.name, mod.version);
        }
        for (const auto& replaced : mods.overrides(gameFolder)) {
            log.info("{} replaces {} of {}", replaced.mod, replaced.file, replaced.replaces);
        }
        setup.settings = Settings::load(roots);

        const auto files = mods.scripts();

        if (!files.empty()) {
            setup.registry = std::make_shared<kuge::ComponentRegistry>();
            kuge::describeCore(*setup.registry);                          // Transform2D...
            setup.registry->describe<Faller>("Faller", {                  // ...and the one thing of this game that a script may use
                kuge::field<Faller>("speed", &Faller::speed, {.min = 0.0, .max = 1000.0, .help = "Pixels per second"}),
                kuge::field<Faller>("value", &Faller::value, {.min = 1.0, .max = 100.0, .help = "Points when caught"}),
            });
            setup.scripts = std::make_shared<kuge::ScriptEngine>(*setup.registry);
            for (const auto& script : files) {
                setup.scripts->load(script.file, script.mod);                // the mod's name is its scope: its globals are its own
                log.info("script {} of the mod {}", script.file.generic_string(), script.mod);
            }
        }
        return setup;
    }

    // -- Systems (Fixed schedule) -------------------------------------------------------------
    class MovePlayer : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                const auto& actions = world.getResource<kuge::ActionState>();
                const float dt = static_cast<float>(world.getResource<kuge::Time>().dt);
                const float speed = world.getResource<Settings>().playerSpeed;
                const float limit = (FIELD_W - PLAYER_W) / 2;

                for (kw::Entity entity : world.view<Player, kuge::Transform2D>()) {
                    auto& x = world.get<kuge::Transform2D>(entity).position.x;

                    x += ((actions.isDown(Action::Right) ? 1.0f : 0.0f) - (actions.isDown(Action::Left) ? 1.0f : 0.0f)) * speed * dt;
                    x = std::max(-limit, std::min(x, limit));
                }
                return true;
            }
    };

    //! The game's own way to make things fall. (A script makes them the same way: an entity with a Transform2D and a Faller.)
    class Rain : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                const auto& time = world.getResource<kuge::Time>();

                m_timer += static_cast<float>(time.dt);
                if (m_timer < world.getResource<Settings>().spawnEvery) {
                    return true;
                }
                m_timer = 0.0f;
                kuge::Random random(kuge::seedFor(0, static_cast<std::uint32_t>(time.tick)));

                world.spawn(kuge::Transform2D{{random.range(-FIELD_W / 2 + 20.0f, FIELD_W / 2 - 20.0f), -FIELD_H / 2 - 20.0f}}, Faller{});
                return true;
            }

        private:
            float m_timer = 0.0f;
    };

    inline float sizeOf(const Faller& faller) { return faller.value > 1 ? 28.0f : 20.0f; }

    //! Things fall, the paddle catches them, the ones that are out of the screen go
    class Fall : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                const float dt = static_cast<float>(world.getResource<kuge::Time>().dt);
                auto& score = world.getResource<Score>();
                kuge::Rect paddle{0.0f, 0.0f, PLAYER_W, PLAYER_H};
                std::vector<kw::Entity> gone;

                for (kw::Entity entity : world.view<Player, kuge::Transform2D>()) {
                    paddle.x = world.get<kuge::Transform2D>(entity).position.x - PLAYER_W / 2;
                    paddle.y = PLAYER_Y - PLAYER_H / 2;
                }
                for (kw::Entity entity : world.view<Faller, kuge::Transform2D>()) {
                    const Faller& faller = world.get<Faller>(entity);
                    auto& position = world.get<kuge::Transform2D>(entity).position;
                    const float size = sizeOf(faller);

                    position.y += faller.speed * dt;
                    if (kuge::Rect{position.x - size / 2, position.y - size / 2, size, size}.intersects(paddle)) {
                        score.points += faller.value;
                        gone.push_back(entity);
                    } else if (position.y > FIELD_H / 2 + 20.0f) {
                        gone.push_back(entity);
                    }
                }
                for (kw::Entity entity : gone) {
                    world.remove(entity);   // after the loop: not while the view is running
                }
                return true;
            }
    };

    //! Gives a look to what has none, whoever made it (the game, or a script that only knows components of the game)
    class Dress : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                std::vector<kw::Entity> naked;

                for (kw::Entity entity : world.view<Faller>()) {
                    if (!world.has<kuge::Sprite>(entity)) {
                        naked.push_back(entity);
                    }
                }
                for (kw::Entity entity : naked) {
                    const Faller& faller = world.get<Faller>(entity);
                    kuge::Sprite sprite;

                    sprite.size = {sizeOf(faller), sizeOf(faller)};
                    sprite.tint = faller.value > 1 ? GOLD : kuge::Color{120, 200, 255, 255};
                    sprite.layer = 1;
                    world.add<kuge::Sprite>(entity, sprite);
                }
                return true;
            }

            static constexpr kuge::Color GOLD{255, 200, 0, 255};
    };

    //! The score is in the title of the window: no font needed
    class ShowScore : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                const int points = world.getResource<Score>().points;

                if (points != m_shown) {
                    m_shown = points;
                    world.getResource<kuge::Ref<kuge::IWindow>>()->setTitle("KUGE Catch - " + std::to_string(points) + " points");
                }
                return true;
            }

        private:
            int m_shown = -1;
    };

    class ExitOnRequest : public kw::ISystem
    {
        public:
            explicit ExitOnRequest(kuge::Engine& engine) : m_engine(engine) {}

            bool handle(kw::World& world) override
            {
                if (world.getResource<kuge::ActionState>().wasPressed(Action::Exit)) {
                    m_engine.stop();
                }
                return true;
            }

        private:
            kuge::Engine& m_engine;
    };

    // -- The scene --------------------------------------------------------------------------------
    class CatchScene : public kuge::ClientScene
    {
        public:
            using kuge::Scene::world;

            explicit CatchScene(Setup setup) : m_setup(std::move(setup)) {}

            void onEnter(void) override
            {
                world().addResource<Score>();
                world().addResource<Settings>(m_setup.settings);
                installClientSystems();
                addSystem(kw::Schedule::Fixed, kuge::stage::Input, std::make_unique<ExitOnRequest>(ctx().engine()));
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<MovePlayer>());
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Rain>());
                if (m_setup.scripts) {
                    // The mods' scripts: `update(dt)` at each tick, after the game's own rain and before things fall
                    addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<kuge::ScriptSystem>(m_setup.scripts));
                }
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<Fall>());
                addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<Dress>());
                addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<ShowScore>());

                const kw::Entity player = world().create();
                kuge::Sprite sprite;

                sprite.size = {PLAYER_W, PLAYER_H};
                sprite.tint = kuge::colors::White;
                sprite.layer = 2;
                world().add<Player>(player, Player{});
                world().add<kuge::Transform2D>(player, kuge::Transform2D{{0.0f, PLAYER_Y}});
                world().add<kuge::Sprite>(player, sprite);
                world().getResource<kuge::Ref<kuge::IWindow>>()->setTitle("KUGE Catch - Left/Right or A/D, Esc to quit");
            }

        private:
            Setup m_setup;
    };

}
