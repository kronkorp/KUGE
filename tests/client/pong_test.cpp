extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Pong.hpp"
#include "backend/dummy/DummyBackend.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    bool near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) < tolerance; }

    // Pong on the dummy backend: the test plays with the keys, one tick per step
    struct Game
    {
        kuge::DummyBackend   dummy;
        kuge::Engine         engine;
        kuge::ClientModule*  client;
        pong::PongScene*     scene = nullptr;

        Game()
            : dummy(kuge::makeDummyBackend({pong::FIELD_W, pong::FIELD_H})),
              engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60}),
              client(&engine.addModule<kuge::ClientModule>(std::move(dummy.backend)))
        {
            pong::bindDefaults(client->input());
            engine.scenes().change<pong::PongScene>();
            engine.step(0.0);
            scene = static_cast<pong::PongScene*>(engine.scenes().top());
        }

        kw::World& world(void) { return scene->world(); }
        pong::Match& match(void) { return world().getResource<pong::Match>(); }

        void key(kuge::Key key, bool down) { dummy.input->push(kuge::KeyEvent{key, down}); }
        void tap(kuge::Key which) { key(which, true); key(which, false); }
        bool ticks(int count)
        {
            bool running = true;

            for (int i = 0; i < count; ++i) {
                running = engine.step(1.0 / 60.0);
            }
            return running;
        }

        kw::Entity ball(void)
        {
            auto view = world().view<pong::Ball>();
            return *view.begin();
        }

        kw::Entity paddle(int side)
        {
            auto view = world().view<pong::Paddle>();

            for (kw::Entity entity : view) {
                if (world().get<pong::Paddle>(entity).side == side) {
                    return entity;
                }
            }
            return 0;
        }

        kuge::Vec2 where(kw::Entity entity) { return world().get<kuge::Transform2D>(entity).position; }
        kuge::Vec2& velocity(void) { return world().get<pong::Ball>(ball()).velocity; }

        // Puts the ball somewhere, moving, in the middle of a rally
        void rally(kuge::Vec2 position, kuge::Vec2 speed)
        {
            world().get<kuge::Transform2D>(ball()).position = position;
            world().get<kuge::PreviousTransform2D>(ball()).value.position = position;
            velocity() = speed;
            match().serving = false;
        }
    };

    std::filesystem::path tempPath(const char* name)
    {
        return std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_" + name);
    }
}

Test(pong, waits_for_the_serve)
{
    Game game;

    Assert(game.match().serving, "waiting");
    Assert(game.where(game.ball()) == kuge::Vec2(0.0f, 0.0f), "the ball is at the center");
    Assert(game.where(game.paddle(0)) == kuge::Vec2(-pong::PADDLE_X, 0.0f), "left paddle");
    Assert(game.where(game.paddle(1)) == kuge::Vec2(pong::PADDLE_X, 0.0f), "right paddle");
    AssertEq(game.match().score[0] + game.match().score[1], 0, "0 - 0");
    game.ticks(30);
    Assert(game.where(game.ball()) == kuge::Vec2(0.0f, 0.0f), "and it stays there until served");
}

Test(pong, serve_launches_the_ball)
{
    Game game;

    game.tap(kuge::Key::Space);
    game.ticks(10);
    Assert(!game.match().serving, "playing");
    Assert(game.velocity().x > 0.0f, "towards the right: the server is player 1");
    Assert(game.where(game.ball()).x > 30.0f, "and it moved");
}

Test(pong, paddles_follow_the_keys)
{
    Game game;

    game.key(kuge::Key::S, true);
    game.key(kuge::Key::Up, true);
    game.ticks(10);
    Assert(near(game.where(game.paddle(0)).y, 70.0f), "P1 went down 10 ticks * 7 px: %f", game.where(game.paddle(0)).y);
    Assert(near(game.where(game.paddle(1)).y, -70.0f), "P2 went up: %f", game.where(game.paddle(1)).y);
    game.key(kuge::Key::S, false);
    game.ticks(10);
    Assert(near(game.where(game.paddle(0)).y, 70.0f), "P1 stopped when the key was released");
    Assert(near(game.where(game.paddle(1)).y, -140.0f), "P2 kept going");
}

Test(pong, paddles_stop_at_the_walls)
{
    Game game;
    const float limit = pong::FIELD_H / 2 - pong::PADDLE_H / 2;

    game.key(kuge::Key::S, true);
    game.key(kuge::Key::Up, true);
    game.ticks(200);
    AssertEq(game.where(game.paddle(0)).y, limit, "P1 at the bottom");
    AssertEq(game.where(game.paddle(1)).y, -limit, "P2 at the top");
}

Test(pong, ball_bounces_on_walls)
{
    Game game;

    game.rally({0.0f, -290.0f}, {0.0f, -300.0f});
    game.ticks(1);
    AssertEq(game.where(game.ball()).y, -(pong::FIELD_H / 2 - pong::BALL_SIZE / 2), "kept in the field");
    Assert(game.velocity().y > 0.0f, "and sent back down");
    game.rally({0.0f, 290.0f}, {0.0f, 300.0f});
    game.ticks(1);
    Assert(game.velocity().y < 0.0f, "up from the bottom wall");
}

Test(pong, paddle_returns_the_ball)
{
    Game game;

    game.rally({330.0f, 0.0f}, {400.0f, 0.0f});
    game.ticks(4);
    Assert(near(game.velocity().x, -400.0f * pong::SPEED_UP), "back, a bit faster: %f", game.velocity().x);
    Assert(near(game.velocity().y, 0.0f), "straight: it hit the middle");
    Assert(game.where(game.ball()).x <= pong::PADDLE_X - pong::PADDLE_W / 2, "and not inside the paddle");
}

Test(pong, tips_send_it_sideways)
{
    Game game;

    game.world().get<kuge::Transform2D>(game.paddle(1)).position.y = 0.0f;
    game.rally({340.0f, 40.0f}, {400.0f, 0.0f});
    game.ticks(3);
    Assert(game.velocity().x < 0.0f, "back");
    Assert(game.velocity().y > 100.0f, "and down, since it hit the lower part: %f", game.velocity().y);
    game.rally({-340.0f, -40.0f}, {-400.0f, 0.0f});
    game.ticks(3);
    Assert(game.velocity().x > 0.0f, "the left paddle returns it to the right");
    Assert(game.velocity().y < -100.0f, "and up");
}

Test(pong, ball_speed_has_a_limit)
{
    Game game;

    game.rally({330.0f, 0.0f}, {700.0f, 0.0f});
    game.ticks(3);
    Assert(near(game.velocity().length(), pong::MAX_SPEED, 0.5f), "no faster than %f, got %f", pong::MAX_SPEED, game.velocity().length());
}

Test(pong, no_bounce_when_leaving)
{
    Game game;

    // Inside the paddle but going away from it (it was hit an instant ago)
    game.rally({350.0f, 0.0f}, {-300.0f, 0.0f});
    game.ticks(1);
    Assert(game.velocity().x < 0.0f && near(game.velocity().length(), 300.0f), "left alone: not slowed, not turned again");
}

Test(pong, a_miss_is_a_point)
{
    Game game;

    game.rally({420.0f, 100.0f}, {400.0f, 0.0f});
    game.ticks(1);
    AssertEq(game.match().score[0], 1, "player 1 scored");
    AssertEq(game.match().score[1], 0, "not player 2");
    Assert(game.match().serving && game.match().server == 0, "player 1 serves");
    Assert(game.where(game.ball()) == kuge::Vec2(0.0f, 0.0f) && game.velocity() == kuge::Vec2(0.0f, 0.0f), "the ball is back, at rest");
    Assert(game.world().get<kuge::PreviousTransform2D>(game.ball()).value == game.world().get<kuge::Transform2D>(game.ball()),
        "and drawn there at once, it does not slide from the edge");

    game.rally({-420.0f, 0.0f}, {-400.0f, 0.0f});
    game.ticks(1);
    AssertEq(game.match().score[1], 1, "player 2 scored on the other side");
    AssertEq(game.match().server, 1, "and serves");
    game.tap(kuge::Key::Space);
    game.ticks(2);
    Assert(game.velocity().x < 0.0f, "towards player 1");
}

Test(pong, nine_wins_and_restarts)
{
    Game game;

    game.match().score = {8, 5};
    game.rally({420.0f, 0.0f}, {400.0f, 0.0f});
    game.ticks(1);
    AssertEq(game.match().score[0], 0, "a new game");
    AssertEq(game.match().score[1], 0, "0 - 0");
}

Test(pong, the_score_is_drawn)
{
    Game game;
    auto squares = [](int digit) {
        int count = 0;

        for (const char* row : pong::FONT[digit]) {
            for (const char* c = row; *c; ++c) {
                count += *c == '1' ? 1 : 0;
            }
        }
        return count;
    };
    const int fixed = 2 + 1 + 15;   // paddles, ball, net

    game.match().score = {3, 7};
    game.ticks(1);
    AssertEq(game.dummy.renderer->lastFrame().size(), static_cast<std::size_t>(fixed + squares(3) + squares(7)),
        "only the squares of the digits are drawn, got %zu", game.dummy.renderer->lastFrame().size());
    game.match().score = {0, 0};
    game.ticks(1);
    AssertEq(game.dummy.renderer->lastFrame().size(), static_cast<std::size_t>(fixed + 2 * squares(0)), "0 - 0");
}

Test(pong, escape_quits)
{
    Game game;

    Assert(game.ticks(2), "still running");
    game.tap(kuge::Key::Escape);
    AssertEq(game.ticks(1), false, "the engine stops");
}

Test(pong, same_keys_same_game)
{
    auto play = [](Game& game) {
        game.tap(kuge::Key::Space);
        game.ticks(20);
        game.key(kuge::Key::S, true);
        game.ticks(35);
        game.key(kuge::Key::S, false);
        game.key(kuge::Key::Up, true);
        game.ticks(60);
        game.key(kuge::Key::Up, false);
        game.ticks(150);
    };
    Game a;
    Game b;

    play(a);
    play(b);
    Assert(a.where(a.ball()) == b.where(b.ball()), "same ball");
    Assert(a.velocity() == b.velocity(), "same speed");
    Assert(a.where(a.paddle(0)) == b.where(b.paddle(0)) && a.where(a.paddle(1)) == b.where(b.paddle(1)), "same paddles");
    Assert(a.match().score == b.match().score, "same score");
    Assert(a.where(a.ball()) != kuge::Vec2(0.0f, 0.0f) || a.match().score[0] + a.match().score[1] > 0, "and something did happen");
}

Test(pong, controls_can_be_rebound)
{
    Game game;

    game.client->input().unbind(pong::Action::P1Down);
    game.client->input().bind(pong::Action::P1Down, kuge::Key::K);
    game.key(kuge::Key::S, true);
    game.ticks(5);
    AssertEq(game.where(game.paddle(0)).y, 0.0f, "S does nothing any more");
    game.key(kuge::Key::S, false);
    game.key(kuge::Key::K, true);
    game.ticks(5);
    Assert(game.where(game.paddle(0)).y > 30.0f, "K moves the paddle down");
}

Test(pong, keybinds_file_changes_them)
{
    const auto path = tempPath("pong_keys.cfg");
    Game game;

    {
        std::ofstream file(path);
        file << "# a player's choice\ninput.p1_down = K\n";
    }
    Assert(game.client->input().loadBindings(path), "the file is loaded");
    std::filesystem::remove(path);
    game.key(kuge::Key::S, true);
    game.ticks(5);
    AssertEq(game.where(game.paddle(0)).y, 0.0f, "S is not bound to it any more");
    game.key(kuge::Key::S, false);
    game.key(kuge::Key::K, true);
    game.ticks(5);
    Assert(game.where(game.paddle(0)).y > 30.0f, "K is");
    game.key(kuge::Key::W, true);
    game.key(kuge::Key::K, false);
    game.ticks(5);
    Assert(game.where(game.paddle(0)).y < 30.0f, "and W, which the file did not mention, still works");
}

Test(pong, a_gamepad_plays_too)
{
    Game game;

    game.dummy.input->push(kuge::GamepadConnectionEvent{3, true});
    game.dummy.input->push(kuge::GamepadAxisEvent{3, kuge::GamepadAxis::LeftY, 0.8f});
    game.ticks(10);
    Assert(game.where(game.paddle(0)).y > 60.0f, "the left stick pushed down moves player 1");
    game.dummy.input->push(kuge::GamepadAxisEvent{3, kuge::GamepadAxis::LeftY, 0.1f});
    game.ticks(1);
    const float stopped = game.where(game.paddle(0)).y;
    game.ticks(5);
    AssertEq(game.where(game.paddle(0)).y, stopped, "and a stick at rest does nothing");
}
