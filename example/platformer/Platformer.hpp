#pragma once

// A small platformer: run, jump, collect the coins, avoid the blobs. Made to use
// every part of the engine at once: a tilemap that scrolls, animations, physics,
// triggers, an interface, sounds, and saves. The core of the game is here, so
// that it can be tested like the engine is (see tests/client/platformer_test.cpp).

#include "Art.hpp"
#include "ClientModule.hpp"
#include "ClientScene.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include "Physics.hpp"
#include "Save.hpp"
#include "Snapshot.hpp"
#include "TilemapCollision.hpp"
#include "UserDirectory.hpp"
#include "render/Tilemap.hpp"
#include "ui/Ui.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace platformer
{

    // -- The player's actions ---------------------------------------------------------------
    enum class Action : std::uint8_t { Left, Right, Jump, Pause, Up, Down, Accept, Click };

    inline void bindDefaults(kuge::InputMap& input)
    {
        using kuge::Binding;
        using kuge::GamepadAxis;
        using kuge::GamepadButton;
        using kuge::Key;

        input.declare(Action::Left, "left");
        input.declare(Action::Right, "right");
        input.declare(Action::Jump, "jump");
        input.declare(Action::Pause, "pause");
        input.declare(Action::Up, "menu_up");
        input.declare(Action::Down, "menu_down");
        input.declare(Action::Accept, "menu_accept");
        input.declare(Action::Click, "menu_click");

        for (Binding b : {Binding(Key::A), Binding(Key::Left), Binding(GamepadButton::DPadLeft), Binding::axis(GamepadAxis::LeftX, -1)}) { input.bind(Action::Left, b); }
        for (Binding b : {Binding(Key::D), Binding(Key::Right), Binding(GamepadButton::DPadRight), Binding::axis(GamepadAxis::LeftX, 1)}) { input.bind(Action::Right, b); }
        for (Binding b : {Binding(Key::Space), Binding(Key::W), Binding(GamepadButton::A)}) { input.bind(Action::Jump, b); }
        for (Binding b : {Binding(Key::Escape), Binding(GamepadButton::Start)}) { input.bind(Action::Pause, b); }
        for (Binding b : {Binding(Key::Up), Binding(Key::W), Binding(GamepadButton::DPadUp)}) { input.bind(Action::Up, b); }
        for (Binding b : {Binding(Key::Down), Binding(Key::S), Binding(GamepadButton::DPadDown)}) { input.bind(Action::Down, b); }
        for (Binding b : {Binding(Key::Enter), Binding(Key::Space), Binding(GamepadButton::A)}) { input.bind(Action::Accept, b); }
        input.bind(Action::Click, kuge::MouseButton::Left);
    }

    inline kuge::UiActions uiActions(void)
    {
        return {.up = kuge::actionId(Action::Up), .down = kuge::actionId(Action::Down), .accept = kuge::actionId(Action::Accept),
                .cancel = kuge::actionId(Action::Pause), .click = kuge::actionId(Action::Click)};
    }

    // -- The world of the game ---------------------------------------------------------------
    constexpr int   LEVEL_W = 60;
    constexpr int   LEVEL_H = 15;
    constexpr float TILE = 16.0f;
    constexpr float ZOOM = 2.0f;
    constexpr float HERO_SPEED = 100.0f;
    constexpr float JUMP_SPEED = 330.0f;
    constexpr float GRAVITY = 900.0f;
    constexpr float MAX_FALL = 400.0f;
    constexpr float WALKER_SPEED = 40.0f;
    constexpr std::uint32_t WORLD_LAYER = 1, HERO_LAYER = 2, ENEMY_LAYER = 4, COIN_LAYER = 8;
    constexpr kuge::Vec2 HERO_START{40.0f, 190.0f};
    constexpr const char* SLOT = "slot1";

    struct Hero { bool facingRight = true; };
    struct Coin { int id = 0; };
    struct Walker { float direction = -1.0f; };

    //! The coins collected so far (a resource: it is saved)
    struct Progress
    {
        std::vector<int> collected;
        int              total = 0;
    };

    struct Sfx
    {
        std::shared_ptr<kuge::Sound> jump, coin, hurt, menu;
    };

    // What the scenes share: where things are, the saves, and how to save
    struct Shared
    {
        std::filesystem::path       font;       //!< Empty: no text
        std::filesystem::path       sounds;
        kuge::SaveSlots             saves;
        kuge::SnapshotRegistry      registry;
        class GameScene*            game = nullptr;   //!< The game that is running, for the pause menu to save it

        Shared(std::filesystem::path fontFile, std::filesystem::path soundFolder, std::filesystem::path saveFolder)
            : font(std::move(fontFile)), sounds(std::move(soundFolder)), saves(std::move(saveFolder), "kuge-platformer", 1)
        {
            registry.component<kuge::Transform2D>("transform",
                [](kuge::ByteWriter& out, const kuge::Transform2D& t) { out.write(t.position.x); out.write(t.position.y); },
                [](kuge::ByteReader& in) { kuge::Transform2D t; t.position.x = in.read<float>(); t.position.y = in.read<float>(); return t; });
            registry.component<Hero>("hero",
                [](kuge::ByteWriter& out, const Hero& h) { out.write(h.facingRight); },
                [](kuge::ByteReader& in) { return Hero{in.read<bool>()}; });
            registry.component<Coin>("coin",
                [](kuge::ByteWriter& out, const Coin& c) { out.write<std::int32_t>(c.id); },
                [](kuge::ByteReader& in) { return Coin{in.read<std::int32_t>()}; });
            registry.resource<Progress>("progress",
                [](kuge::ByteWriter& out, const Progress& p) {
                    out.write<std::int32_t>(p.total);
                    out.write<std::uint32_t>(static_cast<std::uint32_t>(p.collected.size()));
                    for (int id : p.collected) { out.write<std::int32_t>(id); }
                },
                [](kuge::ByteReader& in) {
                    Progress p;

                    p.total = in.read<std::int32_t>();
                    const auto count = in.read<std::uint32_t>();
                    for (std::uint32_t i = 0; i < count; ++i) { p.collected.push_back(in.read<std::int32_t>()); }
                    return p;
                });
        }
    };

    class MenuScene;
    class PauseScene;

    inline kuge::TileMap makeLevel(void)
    {
        kuge::TileMap map(LEVEL_W, LEVEL_H, TILE);
        auto& ground = map.addLayer("ground");
        auto& decor = map.addLayer("decor");
        auto line = [&](int x0, int x1, int y, kuge::TileId tile) { for (int x = x0; x <= x1; ++x) { map.setTile(ground, x, y, tile); } };

        map.setSolid(1);
        map.setSolid(2);
        line(0, LEVEL_W - 1, 13, 1);                 // the ground
        line(0, LEVEL_W - 1, 14, 2);
        line(10, 14, 10, 2);                         // platforms
        line(16, 20, 8, 2);
        line(30, 33, 10, 2);
        line(36, 40, 7, 2);
        for (int x : {18, 26, 45, 50}) {             // low walls, that a jump clears
            map.setTile(ground, x, 12, 2);
            map.setTile(ground, x, 11, 2);
        }
        for (int x : {3, 9, 16, 29, 34, 41, 48, 55}) { map.setTile(decor, x, 12, 3); }
        for (const auto& cloud : {std::pair{4, 5}, std::pair{15, 6}, std::pair{27, 5}, std::pair{42, 4}, std::pair{54, 5}}) {
            map.setTile(decor, cloud.first, cloud.second, 4);
        }
        return map;
    }

    inline const std::vector<kuge::Vec2>& coinPlaces(void)
    {
        static const std::vector<kuge::Vec2> places = {
            {12, 9}, {18, 7}, {32, 9}, {38, 6}, {28, 12}, {47, 12}, {52, 11}, {57, 12},
        };
        return places;
    }

    inline kuge::Vec2 tileCenter(kuge::Vec2 tile) { return {tile.x * TILE + TILE / 2, tile.y * TILE + TILE / 2}; }

    // -- Systems -----------------------------------------------------------------------------
    inline void playSound(kw::World& world, std::shared_ptr<kuge::Sound> Sfx::*which)
    {
        const auto& sfx = world.getResource<Sfx>();

        if (sfx.*which) {
            world.getResource<kuge::Ref<kuge::Audio>>()->play(*(sfx.*which));
        }
    }

    class HeroControl : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                const auto& actions = world.getResource<kuge::ActionState>();
                auto view = world.view<Hero, kuge::Body>();

                for (kw::Entity entity : view) {
                    auto& body = world.get<kuge::Body>(entity);
                    auto& hero = world.get<Hero>(entity);
                    const float direction = (actions.isDown(Action::Right) ? 1.0f : 0.0f) - (actions.isDown(Action::Left) ? 1.0f : 0.0f);

                    body.velocity.x = direction * HERO_SPEED;
                    if (direction != 0.0f) {
                        hero.facingRight = direction > 0.0f;
                    }
                    if (actions.wasPressed(Action::Jump) && body.contacts.down) {
                        body.velocity.y = -JUMP_SPEED;
                        playSound(world, &Sfx::jump);
                    }
                }
                return true;
            }
    };

    // A blob walks until a wall is in its way, then turns round
    class WalkerAi : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                auto view = world.view<Walker, kuge::Body>();

                for (kw::Entity entity : view) {
                    auto& walker = world.get<Walker>(entity);
                    auto& body = world.get<kuge::Body>(entity);

                    if (body.contacts.left) { walker.direction = 1.0f; }
                    if (body.contacts.right) { walker.direction = -1.0f; }
                    body.velocity.x = walker.direction * WALKER_SPEED;
                }
                return true;
            }
    };

    class HeroAnimation : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                auto view = world.view<Hero, kuge::Animator>();

                for (kw::Entity entity : view) {
                    const auto& body = world.get<kuge::Body>(entity);
                    auto& animator = world.get<kuge::Animator>(entity);

                    animator.play(!body.contacts.down ? "jump" : std::fabs(body.velocity.x) > 1.0f ? "run" : "idle");
                    world.get<kuge::Sprite>(entity).flipX = !world.get<Hero>(entity).facingRight;
                }
                return true;
            }
    };

    // Coins are taken, blobs hurt
    class Pickups : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                auto& progress = world.getResource<Progress>();
                const auto& physics = world.getResource<kuge::Physics2D>();

                for (const kuge::TriggerEvent& event : physics.events()) {
                    if (event.entered && world.has<Coin>(event.trigger) && world.has<Hero>(event.other)) {
                        progress.collected.push_back(world.get<Coin>(event.trigger).id);
                        world.remove(event.trigger);
                        playSound(world, &Sfx::coin);
                    }
                }
                auto view = world.view<Hero>();
                for (kw::Entity hero : view) {
                    const auto& transform = world.get<kuge::Transform2D>(hero);
                    const auto& box = world.get<kuge::Collider>(hero);
                    const kuge::Rect area{transform.position.x - box.size.x / 2, transform.position.y - box.size.y / 2, box.size.x, box.size.y};

                    if (!physics.overlapRect(area, ENEMY_LAYER).empty() || transform.position.y > LEVEL_H * TILE + 50.0f) {
                        respawn(world, hero);
                    }
                }
                return true;
            }

        private:
            static void respawn(kw::World& world, kw::Entity hero)
            {
                world.get<kuge::Transform2D>(hero).position = HERO_START;
                world.get<kuge::PreviousTransform2D>(hero).value.position = HERO_START;
                world.get<kuge::Body>(hero).velocity = {};
                playSound(world, &Sfx::hurt);
            }
    };

    // The camera follows the hero as he is drawn (between two ticks), and stops at the ends of the level
    class CameraFollow : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                auto view = world.view<Hero>();
                const kuge::Vec2 screen = world.getResource<kuge::Ref<kuge::IRenderer2D>>()->outputSize();
                auto& camera = world.getResource<kuge::Camera2D>();
                const float alpha = static_cast<float>(world.getResource<kuge::Time>().alpha);
                const float halfW = screen.x / (2.0f * ZOOM);
                const float halfH = screen.y / (2.0f * ZOOM);

                camera.zoom = ZOOM;
                camera.position.y = LEVEL_H * TILE - halfH;
                for (kw::Entity hero : view) {
                    const kuge::Vec2 at = kuge::Vec2::lerp(world.get<kuge::PreviousTransform2D>(hero).value.position,
                        world.get<kuge::Transform2D>(hero).position, alpha);

                    camera.position.x = std::clamp(at.x, halfW, std::max(halfW, LEVEL_W * TILE - halfW));
                }
                return true;
            }
    };

    class Hud : public kw::ISystem
    {
        public:
            Hud(kw::Entity coins, kw::Entity win) : m_coins(coins), m_win(win) {}

            bool handle(kw::World& world) override
            {
                const auto& progress = world.getResource<Progress>();
                const std::string text = std::format("Coins {}/{}", progress.collected.size(), progress.total);
                auto& label = world.get<kuge::UiLabel>(m_coins);

                if (label.text != text) {
                    label.text = text;   // a text that changes is a new picture: only when it does
                }
                world.get<kuge::UiNode>(m_win).visible = progress.total > 0 && static_cast<int>(progress.collected.size()) >= progress.total;
                return true;
            }

        private:
            kw::Entity m_coins;
            kw::Entity m_win;
    };

    // -- Menus -------------------------------------------------------------------------------
    struct MenuEntities
    {
        kw::Entity panel = 0;
        std::vector<kw::Entity> buttons;
    };

    // A title and buttons, in a column in the middle of the screen
    inline MenuEntities buildMenu(kw::World& world, const std::string& title, const std::vector<std::string>& buttons)
    {
        MenuEntities menu;
        kuge::UiPanel panel;

        menu.panel = world.create();
        world.add<kuge::UiNode>(menu.panel, kuge::UiNode{.anchor = kuge::Anchor::Center});
        world.add<kuge::UiPanel>(menu.panel, panel);
        world.add<kuge::UiStack>(menu.panel, kuge::UiStack{.spacing = 10.0f, .padding = 16.0f});
        const kw::Entity heading = world.create();
        kuge::UiLabel label;

        label.text = title;
        label.align = kuge::TextAlign::Center;
        world.add<kuge::UiNode>(heading, kuge::UiNode{.parent = menu.panel, .hasParent = true});
        world.add<kuge::UiLabel>(heading, label);
        for (const std::string& text : buttons) {
            const kw::Entity button = world.create();

            world.add<kuge::UiNode>(button, kuge::UiNode{.parent = menu.panel, .hasParent = true});
            world.add<kuge::UiButton>(button, kuge::UiButton{.text = text});
            menu.buttons.push_back(button);
        }
        return menu;
    }

    inline void loadTheme(kuge::SceneSetup scene, const Shared& shared)
    {
        auto* client = scene.ctx().engine().module<kuge::ClientModule>();

        if (shared.font.empty()) {
            return;
        }
        try {
            scene.world().getResource<kuge::UiTheme>().font = client->loadFont(shared.font, 14);
        } catch (const kuge::FontError& error) {
            Logger::logger().warn("no text: {}", error.what());
        }
    }

    inline Sfx loadSounds(kuge::ClientModule& client, const std::filesystem::path& folder)
    {
        Sfx sfx;

        for (auto [which, name] : {std::pair{&Sfx::jump, "jump.wav"}, std::pair{&Sfx::coin, "coin.wav"},
                                   std::pair{&Sfx::hurt, "hurt.wav"}, std::pair{&Sfx::menu, "menu.wav"}}) {
            try {
                sfx.*which = client.sounds().load(folder / name);
            } catch (const kuge::AudioError& error) {
                Logger::logger().warn("no sound: {}", error.what());
            }
        }
        return sfx;
    }

    // -- The game ----------------------------------------------------------------------------
    class GameScene : public kuge::ClientScene
    {
        public:
            using kuge::Scene::world;

            GameScene(std::shared_ptr<Shared> shared, bool fromSave) : m_shared(std::move(shared)), m_fromSave(fromSave) {}

            void onEnter(void) override;
            void onExit(void) override
            {
                if (m_shared->game == this) {
                    m_shared->game = nullptr;
                }
            }

            //! Keeps the game in the slot
            void save(void)
            {
                const auto& progress = world().getResource<Progress>();
                kuge::ByteWriter out;

                m_shared->registry.save(world(), out);
                m_shared->saves.write(SLOT, std::format("{} of {} coins", progress.collected.size(), progress.total), out.bytes());
            }

        private:
            void equipHero(kw::Entity hero);
            void equipCoin(kw::Entity coin);
            void spawnWalker(kuge::Vec2 at);
            void addLevel(void);
            bool load(void);

            std::shared_ptr<Shared>                m_shared;
            bool                                   m_fromSave;
            std::shared_ptr<kuge::Spritesheet>     m_heroSheet, m_coinSheet, m_blobSheet;
            std::shared_ptr<const kuge::AnimationSet> m_clips;
    };

    class PauseScene : public kuge::ClientScene
    {
        public:
            using kuge::Scene::world;

            explicit PauseScene(std::shared_ptr<Shared> shared) : m_shared(std::move(shared)) {}

            void onEnter(void) override;

        private:
            std::shared_ptr<Shared> m_shared;
    };

    class MenuScene : public kuge::ClientScene
    {
        public:
            using kuge::Scene::world;

            explicit MenuScene(std::shared_ptr<Shared> shared) : m_shared(std::move(shared)) {}

            void onEnter(void) override;

        private:
            std::shared_ptr<Shared> m_shared;
    };

    class PauseRequest : public kw::ISystem
    {
        public:
            PauseRequest(kuge::SceneManager& scenes, std::shared_ptr<Shared> shared) : m_scenes(scenes), m_shared(std::move(shared)) {}

            bool handle(kw::World& world) override
            {
                if (world.getResource<kuge::ActionState>().wasPressed(Action::Pause)) {
                    m_scenes.push<PauseScene>(m_shared);
                }
                return true;
            }

        private:
            kuge::SceneManager&     m_scenes;
            std::shared_ptr<Shared> m_shared;
    };

    // Back to the menu when the game is won
    class BackToMenu : public kw::ISystem
    {
        public:
            BackToMenu(kuge::SceneManager& scenes, std::shared_ptr<Shared> shared) : m_scenes(scenes), m_shared(std::move(shared)) {}

            bool handle(kw::World& world) override
            {
                const auto& progress = world.getResource<Progress>();

                if (progress.total > 0 && static_cast<int>(progress.collected.size()) >= progress.total
                    && world.getResource<kuge::ActionState>().wasPressed(Action::Accept)) {
                    m_scenes.change<MenuScene>(m_shared);
                }
                return true;
            }

        private:
            kuge::SceneManager&     m_scenes;
            std::shared_ptr<Shared> m_shared;
    };

    // What the buttons of a menu do
    class MenuLogic : public kw::ISystem
    {
        public:
            using Handler = std::function<void(std::size_t)>;   // by the number of the button

            MenuLogic(std::vector<kw::Entity> buttons, Handler onPressed, std::function<void(void)> onBack = {})
                : m_buttons(std::move(buttons)), m_pressed(std::move(onPressed)), m_back(std::move(onBack)) {}

            bool handle(kw::World& world) override
            {
                // Copied: a handler may change the scene, and this list with it
                const auto events = world.getResource<kuge::UiEvents>().list;

                for (const kuge::UiEvent& event : events) {
                    if (event.kind == kuge::UiEvent::Kind::Activated) {
                        const auto found = std::find(m_buttons.begin(), m_buttons.end(), event.entity);

                        if (found != m_buttons.end()) {
                            playSound(world, &Sfx::menu);
                            m_pressed(static_cast<std::size_t>(found - m_buttons.begin()));
                        }
                    } else if (event.kind == kuge::UiEvent::Kind::Cancelled && m_back) {
                        m_back();
                    }
                }
                return true;
            }

        private:
            std::vector<kw::Entity>   m_buttons;
            Handler                   m_pressed;
            std::function<void(void)> m_back;
    };

    inline void GameScene::equipHero(kw::Entity hero)
    {
        kuge::Body body;
        kuge::Sprite sprite;

        body.type = kuge::Body::Type::Dynamic;
        sprite.size = {16.0f, 16.0f};
        sprite.layer = 2;
        world().add<kuge::Collider>(hero, kuge::Collider::box(10.0f, 14.0f).onLayer(HERO_LAYER, WORLD_LAYER | COIN_LAYER));   // a trigger only sees what meets it too
        world().add<kuge::Body>(hero, body);
        world().add<kuge::Sprite>(hero, sprite);
        world().add<kuge::Animator>(hero, kuge::Animator::of(m_heroSheet, m_clips, "idle"));
        world().add<kuge::PreviousTransform2D>(hero, kuge::PreviousTransform2D{world().get<kuge::Transform2D>(hero)});
    }

    inline void GameScene::equipCoin(kw::Entity coin)
    {
        kuge::Sprite sprite;

        sprite.size = {16.0f, 16.0f};
        sprite.layer = 1;
        world().add<kuge::Collider>(coin, kuge::Collider::circle(5.0f).onLayer(COIN_LAYER, HERO_LAYER).asTrigger());
        world().add<kuge::Sprite>(coin, sprite);
        world().add<kuge::Animator>(coin, kuge::Animator::of(m_coinSheet, m_clips, "spin"));
    }

    inline void GameScene::spawnWalker(kuge::Vec2 at)
    {
        const kw::Entity blob = world().create();
        kuge::Body body;
        kuge::Sprite sprite;

        body.type = kuge::Body::Type::Kinematic;
        sprite.size = {16.0f, 16.0f};
        sprite.layer = 2;
        world().add<kuge::Transform2D>(blob, kuge::Transform2D{at});
        world().add<kuge::PreviousTransform2D>(blob, kuge::PreviousTransform2D{{at}});
        world().add<Walker>(blob, Walker{});
        world().add<kuge::Collider>(blob, kuge::Collider::box(12.0f, 10.0f).onLayer(ENEMY_LAYER, WORLD_LAYER));
        world().add<kuge::Body>(blob, body);
        world().add<kuge::Sprite>(blob, sprite);
        world().add<kuge::Animator>(blob, kuge::Animator::of(m_blobSheet, m_clips, "walk"));
    }

    inline void GameScene::addLevel(void)
    {
        auto map = std::make_shared<kuge::TileMap>(makeLevel());
        auto tiles = makeTiles(*world().getResource<kuge::Ref<kuge::IRenderer2D>>());

        world().getResource<kuge::Physics2D>().tiles = kuge::makeSolidGrid(*map, "ground");
        for (auto [layer, draw] : {std::pair{"decor", -10}, std::pair{"ground", -5}}) {
            const kw::Entity view = world().create();

            world().add<kuge::Transform2D>(view, kuge::Transform2D{});
            world().add<kuge::TilemapView>(view, kuge::TilemapView{map, tiles, layer, draw});
        }
    }

    // Puts back the game that was saved: the hero and the coins that are left
    inline bool GameScene::load(void)
    {
        try {
            const auto data = m_shared->saves.read(SLOT);
            kuge::ByteReader in(data.payload);

            for (kw::Entity entity : m_shared->registry.load(world(), in)) {
                if (world().has<Hero>(entity)) {
                    equipHero(entity);
                } else if (world().has<Coin>(entity)) {
                    equipCoin(entity);
                }
            }
            return true;
        } catch (const kuge::SaveError& error) {
            Logger::logger().error("cannot load the game: {}", error.what());
        } catch (const kuge::SerializerError& error) {
            Logger::logger().error("cannot load the game: {}", error.what());
        }
        return false;
    }

    inline void GameScene::onEnter(void)
    {
        auto* client = ctx().engine().module<kuge::ClientModule>();
        auto& renderer = client->renderer();

        installClientSystems();
        kuge::installPhysics(setup(), kuge::PhysicsConfig{.gravity = {0.0f, GRAVITY}, .maxFallSpeed = MAX_FALL});
        kuge::installUi(setup(), uiActions());
        loadTheme(setup(), *m_shared);
        world().addResource<Sfx>(loadSounds(*client, m_shared->sounds));
        world().addResource<Progress>();
        m_shared->game = this;
        m_heroSheet = makeHero(renderer);
        m_coinSheet = makeCoin(renderer);
        m_blobSheet = makeBlob(renderer);
        m_clips = makeClips();
        addLevel();

        if (!(m_fromSave && load())) {
            const kw::Entity hero = world().create();

            world().add<kuge::Persistent>(hero, kuge::Persistent{});
            world().add<kuge::Transform2D>(hero, kuge::Transform2D{HERO_START});
            world().add<Hero>(hero, Hero{});
            equipHero(hero);
            int id = 0;
            for (const kuge::Vec2& tile : coinPlaces()) {
                const kw::Entity coin = world().create();

                world().add<kuge::Persistent>(coin, kuge::Persistent{});
                world().add<kuge::Transform2D>(coin, kuge::Transform2D{tileCenter(tile)});
                world().add<Coin>(coin, Coin{id++});
                equipCoin(coin);
            }
            world().getResource<Progress>().total = id;
        }
        spawnWalker({360.0f, 203.0f});
        spawnWalker({768.0f, 203.0f});

        // The interface: the coins, and a message when they are all there
        const kw::Entity coins = world().create();
        const kw::Entity win = world().create();
        kuge::UiLabel counter, message;

        counter.text = "Coins";
        message.text = "All the coins! Press Enter";
        message.align = kuge::TextAlign::Center;
        world().add<kuge::UiNode>(coins, kuge::UiNode{.anchor = kuge::Anchor::TopLeft, .offset = {10.0f, 8.0f}});
        world().add<kuge::UiLabel>(coins, counter);
        world().add<kuge::UiNode>(win, kuge::UiNode{.anchor = kuge::Anchor::Center, .visible = false});
        world().add<kuge::UiLabel>(win, message);

        addSystem(kw::Schedule::Fixed, kuge::stage::Input, std::make_unique<PauseRequest>(ctx().scenes(), m_shared));
        addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<HeroControl>());
        addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<WalkerAi>());
        addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<Pickups>());
        addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<HeroAnimation>());
        addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<Hud>(coins, win));
        addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<BackToMenu>(ctx().scenes(), m_shared));
        addSystem(kw::Schedule::Frame, kuge::stage::Late, std::make_unique<CameraFollow>());
    }

    inline void PauseScene::onEnter(void)
    {
        auto* client = ctx().engine().module<kuge::ClientModule>();

        installClientSystems();
        kuge::installUi(setup(), uiActions());
        loadTheme(setup(), *m_shared);
        world().addResource<Sfx>(loadSounds(*client, m_shared->sounds));
        const auto menu = buildMenu(world(), "Paused", {"Resume", "Save game", "Main menu"});
        auto& scenes = ctx().scenes();
        auto shared = m_shared;

        addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<MenuLogic>(menu.buttons,
            [&scenes, shared](std::size_t button) {
                if (button == 0) {
                    scenes.pop();
                } else if (button == 1) {
                    if (shared->game) {
                        shared->game->save();
                    }
                    scenes.pop();
                } else {
                    scenes.change<MenuScene>(shared);
                }
            },
            [&scenes] { scenes.pop(); }));
    }

    inline void MenuScene::onEnter(void)
    {
        auto* client = ctx().engine().module<kuge::ClientModule>();

        installClientSystems();
        kuge::installUi(setup(), uiActions());
        loadTheme(setup(), *m_shared);
        world().addResource<Sfx>(loadSounds(*client, m_shared->sounds));
        const auto menu = buildMenu(world(), "KUGE Platformer", {"New game", "Continue", "Quit"});
        auto& scenes = ctx().scenes();
        auto& engine = ctx().engine();
        auto shared = m_shared;

        world().get<kuge::UiButton>(menu.buttons[1]).enabled = m_shared->saves.exists(SLOT);
        addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<MenuLogic>(menu.buttons,
            [&scenes, &engine, shared](std::size_t button) {
                if (button == 0) {
                    scenes.change<GameScene>(shared, false);
                } else if (button == 1) {
                    scenes.change<GameScene>(shared, true);
                } else {
                    engine.stop();
                }
            }));
    }

}
