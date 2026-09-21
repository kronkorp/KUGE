extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Platformer.hpp"
#include "backend/dummy/DummyBackend.hpp"
#include "ui_fixture.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using namespace platformer;

    struct TempDir
    {
        std::filesystem::path path;

        explicit TempDir(const char* name)
            : path(std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_" + name))
        {
            std::filesystem::remove_all(path);
            std::filesystem::create_directories(path);
        }
        ~TempDir() { std::filesystem::remove_all(path); }
    };

    // The whole game on the dummy backend: the test plays with the keys, one tick per step
    struct Game
    {
        TempDir                  dir;
        FontFile                 font;
        std::shared_ptr<Shared>  shared;
        kuge::DummyBackend       dummy;
        kuge::Engine             engine;
        kuge::ClientModule*      client;

        explicit Game(const char* name, bool inGame = true)
            : dir(name),
              dummy(kuge::makeDummyBackend({640.0f, 360.0f})),
              engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60}),
              client(nullptr)
        {
            writeSounds(dir.path / "sfx");
            shared = std::make_shared<Shared>(font.path, dir.path / "sfx", dir.path / "saves");
            client = &engine.addModule<kuge::ClientModule>(std::move(dummy.backend));
            bindDefaults(client->input());
            if (inGame) {
                engine.scenes().change<GameScene>(shared, false);
            } else {
                engine.scenes().change<MenuScene>(shared);
            }
            engine.step(0.0);
        }

        template<typename T> T* scene(void) { return dynamic_cast<T*>(engine.scenes().top()); }
        kw::World& world(void)
        {
            kuge::Scene* top = engine.scenes().top();

            if (auto* game = dynamic_cast<GameScene*>(top)) { return game->world(); }
            if (auto* pause = dynamic_cast<PauseScene*>(top)) { return pause->world(); }
            return static_cast<MenuScene*>(top)->world();
        }

        void ticks(int count) { for (int i = 0; i < count; ++i) { engine.step(1.0 / 60.0); } }
        void key(kuge::Key k, bool down) { dummy.input->push(kuge::KeyEvent{k, down}); }
        void tap(kuge::Key k) { key(k, true); key(k, false); ticks(1); }

        // The hero of a game scene
        kw::Entity hero(void) { return *world().view<Hero>().begin(); }
        kuge::Vec2 where(void) { return world().get<kuge::Transform2D>(hero()).position; }
        kuge::Body& body(void) { return world().get<kuge::Body>(hero()); }
        void put(kuge::Vec2 at)
        {
            world().get<kuge::Transform2D>(hero()).position = at;
            world().get<kuge::PreviousTransform2D>(hero()).value.position = at;
            body().velocity = {};
        }
        Progress& progress(void) { return world().getResource<Progress>(); }

        std::size_t coins(void)
        {
            std::size_t count = 0;

            for ([[maybe_unused]] kw::Entity e : world().view<Coin>()) { ++count; }
            return count;
        }

        // Lets the hero fall and stand
        void land(void) { ticks(30); }

        kuge::Vec2 coinAt(int id) { return tileCenter(coinPlaces()[static_cast<std::size_t>(id)]); }
    };
}

Test(platformer, the_hero_lands)
{
    Game game("pf_land");

    game.land();
    Assert(std::fabs(game.where().y - 201.0f) < 0.05f, "on the ground (its feet at 208): y = %f", game.where().y);
    Assert(game.body().contacts.down, "standing");
    AssertStrEq(std::string(game.world().get<kuge::Animator>(game.hero()).current()).c_str(), "idle", "and at rest");
}

Test(platformer, running)
{
    Game game("pf_run");

    game.land();
    const float x = game.where().x;
    game.key(kuge::Key::D, true);
    game.ticks(30);
    Assert(std::fabs(game.where().x - (x + 50.0f)) < 1.0f, "30 ticks at 100 px/s: 50 further, got %f", game.where().x - x);
    AssertStrEq(std::string(game.world().get<kuge::Animator>(game.hero()).current()).c_str(), "run", "running");
    Assert(!game.world().get<kuge::Sprite>(game.hero()).flipX, "facing right");
    game.key(kuge::Key::D, false);
    game.key(kuge::Key::A, true);
    game.ticks(10);
    Assert(game.world().get<kuge::Sprite>(game.hero()).flipX, "and left");
    Assert(game.where().x < x + 50.0f, "going back");
    game.key(kuge::Key::A, false);
    game.ticks(2);
    AssertStrEq(std::string(game.world().get<kuge::Animator>(game.hero()).current()).c_str(), "idle", "at rest again");
}

Test(platformer, jumping)
{
    Game game("pf_jump");

    game.land();
    const float ground = game.where().y;
    float highest = ground;

    game.tap(kuge::Key::Space);
    AssertEq(game.dummy.audio->activeVoices(), 1, "the jump makes a sound");
    for (int i = 0; i < 25; ++i) {
        game.ticks(1);
        highest = std::min(highest, game.where().y);
        if (i == 5) {
            AssertStrEq(std::string(game.world().get<kuge::Animator>(game.hero()).current()).c_str(), "jump", "in the air: the jump pose");
            game.tap(kuge::Key::Space);          // in the air: nothing happens
        }
    }
    Assert(ground - highest > 57.0f && ground - highest < 62.0f, "about 60 pixels high (330 px/s, 900 px/s2): %f", ground - highest);
    game.ticks(30);
    Assert(game.body().contacts.down && std::fabs(game.where().y - ground) < 0.05f, "and lands where it started");
}

Test(platformer, no_double_jump)
{
    Game game("pf_double");

    game.land();
    game.tap(kuge::Key::Space);
    game.ticks(15);
    const float vy = game.body().velocity.y;
    game.tap(kuge::Key::Space);
    Assert(game.body().velocity.y > vy, "a jump in the air does not push up again: the speed only grows with the fall");
}

Test(platformer, walls_stop_the_hero)
{
    Game game("pf_wall");

    game.land();
    game.put({250.0f, 201.0f});
    game.key(kuge::Key::D, true);
    game.ticks(120);
    Assert(std::fabs(game.where().x - 283.0f) < 0.05f, "against the wall whose face is at 288: x = %f", game.where().x);
    Assert(game.body().contacts.right, "touching it");
    game.tap(kuge::Key::Space);
    game.ticks(50);
    Assert(game.where().x > 300.0f, "but a jump clears it: %f", game.where().x);
}

Test(platformer, taking_a_coin)
{
    Game game("pf_coin");

    AssertEq(game.coins(), 8, "8 coins");
    AssertEq(game.progress().total, 8, "of 8");
    game.put(game.coinAt(3));
    game.ticks(2);
    AssertEq(game.coins(), 7, "one was taken");
    Assert(game.progress().collected == std::vector<int>{3}, "the third");
    AssertEq(game.dummy.audio->activeVoices(), 1, "with a sound");
    game.put(game.coinAt(3));
    game.ticks(2);
    AssertEq(game.progress().collected.size(), 1, "it cannot be taken twice");
    game.put(game.coinAt(0));
    game.ticks(2);
    AssertEq(game.progress().collected.size(), 2, "another one");
}

Test(platformer, the_counter)
{
    Game game("pf_counter");
    auto text = [&game] {
        for (kw::Entity e : game.world().view<kuge::UiLabel>()) {
            if (game.world().get<kuge::UiLabel>(e).text.rfind("Coins", 0) == 0) {
                return game.world().get<kuge::UiLabel>(e).text;
            }
        }
        return std::string();
    };

    game.ticks(2);
    AssertStrEq(text().c_str(), "Coins 0/8", "at the start");
    game.put(game.coinAt(5));
    game.ticks(2);
    AssertStrEq(text().c_str(), "Coins 1/8", "and after one");
}

Test(platformer, winning)
{
    Game game("pf_win");

    for (int id = 0; id < 8; ++id) {
        game.put(game.coinAt(id));
        game.ticks(2);
    }
    AssertEq(game.coins(), 0, "all taken");
    game.ticks(2);
    bool shown = false;
    for (kw::Entity e : game.world().view<kuge::UiLabel>()) {
        if (game.world().get<kuge::UiLabel>(e).text.rfind("All", 0) == 0) {
            shown = game.world().get<kuge::UiNode>(e).visible;
        }
    }
    Assert(shown, "the message shows");
    game.tap(kuge::Key::Enter);
    Assert(game.scene<MenuScene>() != nullptr, "and Enter goes back to the menu");
}

Test(platformer, blobs_hurt)
{
    Game game("pf_hurt");

    game.land();
    kw::Entity blob = *game.world().view<Walker>().begin();
    game.put(game.world().get<kuge::Transform2D>(blob).position);   // on a blob
    game.ticks(2);
    Assert(std::fabs(game.where().x - HERO_START.x) < 3.0f, "back at the start: %f", game.where().x);
    Assert(game.dummy.audio->activeVoices() >= 1, "with a sound");
    game.put({100.0f, 900.0f});                    // fell out of the level
    game.ticks(2);
    Assert(game.where().y < 300.0f, "falling out of the level also starts again");
}

Test(platformer, blobs_turn_at_walls)
{
    Game game("pf_blobs");
    float lowest = 1e9f, highest = -1e9f;
    int turns = 0;
    float last = 0.0f;
    kw::Entity blob = 0;

    game.ticks(1);
    for (kw::Entity e : game.world().view<Walker>()) {
        if (game.world().get<kuge::Transform2D>(e).position.x < 500.0f) {
            blob = e;
        }
    }
    for (int i = 0; i < 900; ++i) {
        game.ticks(1);
        const float x = game.world().get<kuge::Transform2D>(blob).position.x;
        const float direction = game.world().get<Walker>(blob).direction;

        lowest = std::min(lowest, x);
        highest = std::max(highest, x);
        turns += (direction != last && i > 0) ? 1 : 0;
        last = direction;
    }
    // Between the walls of tiles 18 and 26: from x = 304 to 416, and the blob is 12 wide
    Assert(lowest > 309.0f && highest < 411.0f, "it stays between its walls: %f to %f", lowest, highest);
    Assert(turns >= 4, "and turns round again and again: %d", turns);
}

Test(platformer, the_camera_follows)
{
    Game game("pf_camera");

    game.land();
    game.ticks(2);
    const auto& camera = game.world().getResource<kuge::Camera2D>();
    AssertEq(camera.zoom, 2.0f, "the game is shown at zoom 2");
    AssertEq(camera.position.x, 160.0f, "at the start it stops at the left end of the level (half a screen)");
    game.put({500.0f, 201.0f});
    game.ticks(3);
    Assert(std::fabs(camera.position.x - 500.0f) < 0.5f, "in the middle it is on the hero: %f", camera.position.x);
    game.put({950.0f, 201.0f});
    game.ticks(3);
    AssertEq(camera.position.x, 800.0f, "and at the right end it stops: 960 - 160");
    AssertEq(camera.position.y, 15.0f * 16.0f - 90.0f, "the bottom of the level is at the bottom of the screen");
}

Test(platformer, the_screen_shows_the_game)
{
    Game game("pf_screen");

    game.ticks(3);
    const auto& calls = game.dummy.renderer->lastFrame();
    std::size_t textures = 0, fills = 0;

    for (const auto& call : calls) {
        (call.kind == kuge::DummyRenderer::Call::Kind::Texture ? textures : fills) += 1;
    }
    Assert(textures > 30, "the tiles, the sprites and the text are drawn: %zu pictures", textures);
    AssertEq(fills, 0, "and there is no box left: no menu is open");
}

Test(platformer, pause_and_resume)
{
    Game game("pf_pause");

    game.land();
    game.tap(kuge::Key::Escape);
    Assert(game.engine.scenes().size() == 2 && game.scene<PauseScene>() != nullptr, "Escape pauses: a scene over the game");
    game.key(kuge::Key::D, true);
    game.ticks(30);
    game.key(kuge::Key::D, false);
    game.tap(kuge::Key::Escape);
    Assert(game.engine.scenes().size() == 1 && game.scene<GameScene>() != nullptr, "Escape again: back to the game");
    Assert(std::fabs(game.where().x - HERO_START.x) < 1.0f, "and the game did not move while paused: %f", game.where().x);
    game.tap(kuge::Key::Escape);
    game.ticks(2);
    game.tap(kuge::Key::Enter);
    Assert(game.scene<GameScene>() != nullptr, "the first button, Resume, goes back too");
}

Test(platformer, saving_and_loading)
{
    Game game("pf_save");

    game.land();
    for (int id : {0, 3}) {
        game.put(game.coinAt(id));
        game.ticks(2);
    }
    game.put({300.0f, 201.0f});
    game.ticks(2);
    Assert(!game.shared->saves.exists(SLOT), "nothing saved yet");
    game.tap(kuge::Key::Escape);
    game.ticks(2);
    game.tap(kuge::Key::Down);                       // Save game
    game.tap(kuge::Key::Enter);
    Assert(game.shared->saves.exists(SLOT), "the slot is written");
    Assert(game.shared->saves.read(SLOT).info.label == "2 of 8 coins", "with a label for the menu: '%s'", game.shared->saves.read(SLOT).info.label.c_str());
    Assert(game.scene<GameScene>() != nullptr, "and the game goes on");

    // To the menu, and continue
    game.tap(kuge::Key::Escape);
    game.ticks(2);
    game.tap(kuge::Key::Down);
    game.tap(kuge::Key::Down);                       // Main menu
    game.tap(kuge::Key::Enter);
    Assert(game.scene<MenuScene>() != nullptr, "back at the menu");
    game.ticks(2);
    game.tap(kuge::Key::Down);                       // Continue, now that there is a save
    game.tap(kuge::Key::Enter);
    Assert(game.scene<GameScene>() != nullptr, "the saved game starts");
    game.ticks(2);
    AssertEq(game.coins(), 6, "with the coins that were left");
    Assert(game.progress().collected == std::vector<int>({0, 3}) && game.progress().total == 8, "and what was collected");
    Assert(std::fabs(game.where().x - 300.0f) < 3.0f, "the hero where he was: %f", game.where().x);
}

Test(platformer, a_damaged_save)
{
    Game game("pf_damaged");

    game.land();
    game.tap(kuge::Key::Escape);
    game.ticks(2);
    game.tap(kuge::Key::Down);
    game.tap(kuge::Key::Enter);                       // a good save
    auto bytes = kuge::readFile(game.shared->saves.directory() / "slot1.ksave");
    bytes[bytes.size() / 2] ^= 0xFF;
    kuge::writeFile(game.shared->saves.directory() / "slot1.ksave", bytes);

    game.engine.scenes().change<GameScene>(game.shared, true);
    game.ticks(3);
    AssertEq(game.coins(), 8, "a save that cannot be read: a new game, not a crash");
    Assert(std::fabs(game.where().x - HERO_START.x) < 3.0f, "at the start");
}

Test(platformer, the_menu)
{
    Game game("pf_menu", false);

    game.ticks(2);
    const auto& state = game.world().getResource<kuge::UiState>();
    Assert(state.hasFocus, "something is selected");
    game.tap(kuge::Key::Down);
    Assert(game.world().get<kuge::UiButton>(state.focus).text == "Quit", "Continue is disabled with no save: down goes to Quit");
    game.tap(kuge::Key::Up);
    game.tap(kuge::Key::Enter);
    Assert(game.scene<GameScene>() != nullptr, "New game starts a game");
    game.tap(kuge::Key::Escape);
    game.ticks(2);
    game.tap(kuge::Key::Down);
    game.tap(kuge::Key::Down);
    game.tap(kuge::Key::Enter);
    game.ticks(2);
    game.tap(kuge::Key::Down);
    game.tap(kuge::Key::Enter);
    AssertEq(game.engine.step(1.0 / 60.0), false, "Quit stops the engine");
}

Test(platformer, same_keys_same_game)
{
    auto play = [](Game& game) {
        game.key(kuge::Key::D, true);
        for (int tick = 0; tick < 500; ++tick) {
            if (tick % 37 == 0) {
                game.key(kuge::Key::Space, true);
                game.key(kuge::Key::Space, false);
            }
            game.ticks(1);
        }
    };
    Game a("pf_det_a");
    Game b("pf_det_b");

    play(a);
    play(b);
    Assert(a.where() == b.where(), "the same place, bit for bit: %f, %f", a.where().x, a.where().y);
    Assert(a.progress().collected == b.progress().collected, "the same coins");
    Assert(a.where().x > 100.0f, "and the hero did go somewhere (%f)", a.where().x);
}

// Every platform must be reachable by jumping: a step is at most 3 tiles, and gaps are short
namespace
{
    // Tries jumps from a few places on a floor, running towards the target, and tells if one lands
    // on a floor whose top is at targetTop
    bool canLandOn(float fromLeft, float fromRight, float floorTop, float targetTop)
    {
        for (float x = fromLeft; x <= fromRight; x += 4.0f) {
            for (int runUp : {0, 6, 12}) {
                Game game("pf_reach");

                game.land();
                game.put({x, floorTop - 7.0f});
                game.ticks(2);
                game.key(kuge::Key::D, true);
                game.ticks(runUp);
                game.tap(kuge::Key::Space);
                for (int i = 0; i < 70; ++i) {
                    game.ticks(1);
                    if (game.body().contacts.down && std::fabs(game.where().y - (targetTop - 7.0f)) < 0.5f) {
                        return true;
                    }
                }
            }
        }
        return false;
    }
}

Test(platformer, platforms_reachable)
{
    Assert(canLandOn(100.0f, 156.0f, 208.0f, 160.0f), "the first platform, from the ground");
    Assert(canLandOn(200.0f, 236.0f, 160.0f, 128.0f), "the high one, from the first");
    Assert(canLandOn(420.0f, 476.0f, 208.0f, 160.0f), "the platform at 30-33, from the ground");
    Assert(canLandOn(520.0f, 540.0f, 160.0f, 112.0f), "the last one, from the platform before it");
}
