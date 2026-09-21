#pragma once

// Pong for two, on one keyboard. It is the first game made with KUGE: it only
// uses the core and the client module, and it never sees a key, only actions.

#include "ClientModule.hpp"
#include "ClientScene.hpp"
#include "Engine.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace pong
{

    // -- What the player can ask for ------------------------------------------------------
    enum class Action : std::uint8_t { P1Up, P1Down, P2Up, P2Down, Serve, Exit };

    // Defaults, that a keybinds file can change (see kuge::InputMap::loadBindings)
    inline void bindDefaults(kuge::InputMap& input)
    {
        input.declare(Action::P1Up, "p1_up");
        input.declare(Action::P1Down, "p1_down");
        input.declare(Action::P2Up, "p2_up");
        input.declare(Action::P2Down, "p2_down");
        input.declare(Action::Serve, "serve");
        input.declare(Action::Exit, "exit");

        input.bind(Action::P1Up, kuge::Key::W);
        input.bind(Action::P1Up, kuge::GamepadButton::DPadUp);
        input.bind(Action::P1Up, kuge::Binding::axis(kuge::GamepadAxis::LeftY, -1));
        input.bind(Action::P1Down, kuge::Key::S);
        input.bind(Action::P1Down, kuge::GamepadButton::DPadDown);
        input.bind(Action::P1Down, kuge::Binding::axis(kuge::GamepadAxis::LeftY, 1));
        input.bind(Action::P2Up, kuge::Key::Up);
        input.bind(Action::P2Up, kuge::Binding::axis(kuge::GamepadAxis::RightY, -1));
        input.bind(Action::P2Down, kuge::Key::Down);
        input.bind(Action::P2Down, kuge::Binding::axis(kuge::GamepadAxis::RightY, 1));
        input.bind(Action::Serve, kuge::Key::Space);
        input.bind(Action::Serve, kuge::GamepadButton::A);
        input.bind(Action::Exit, kuge::Key::Escape);
    }

    // -- The field: the origin is its center, y points down --------------------------------
    constexpr float FIELD_W      = 800.0f;
    constexpr float FIELD_H      = 600.0f;
    constexpr float PADDLE_W     = 16.0f;
    constexpr float PADDLE_H     = 100.0f;
    constexpr float PADDLE_X     = 360.0f;    // distance of a paddle from the center
    constexpr float PADDLE_SPEED = 420.0f;    // pixels per second
    constexpr float BALL_SIZE    = 16.0f;
    constexpr float SERVE_SPEED  = 320.0f;
    constexpr float MAX_SPEED    = 720.0f;
    constexpr float SPEED_UP     = 1.06f;     // each hit of a paddle
    constexpr float MAX_ANGLE    = 1.05f;     // radians, for a hit at the tip of a paddle
    constexpr int   POINTS_TO_WIN = 9;

    // -- What makes the game ---------------------------------------------------------------
    struct Paddle { int side; };                     //!< 0: left, 1: right
    struct Ball { kuge::Vec2 velocity; };
    struct Cell { int player; int row; int col; };   //!< A square of a digit of the score

    //! A resource of the World
    struct Match
    {
        std::array<int, 2> score{0, 0};
        bool               serving = true;   //!< The ball waits for Serve
        int                server = 0;       //!< Who serves: the last one to score
    };

    // A digit is 3 squares wide and 5 high
    constexpr const char* FONT[10][5] = {
        {"111", "101", "101", "101", "111"}, {"010", "110", "010", "010", "111"},
        {"111", "001", "111", "100", "111"}, {"111", "001", "111", "001", "111"},
        {"101", "101", "111", "001", "001"}, {"111", "100", "111", "001", "111"},
        {"111", "100", "111", "101", "111"}, {"111", "001", "001", "001", "001"},
        {"111", "101", "111", "101", "111"}, {"111", "101", "111", "001", "111"},
    };

    inline float clampf(float value, float low, float high) { return std::max(low, std::min(value, high)); }

    // -- Systems (Fixed schedule) ----------------------------------------------------------
    class MovePaddles : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                const auto& actions = world.getResource<kuge::ActionState>();
                const float dt = static_cast<float>(world.getResource<kuge::Time>().dt);
                const float limit = FIELD_H / 2 - PADDLE_H / 2;
                auto view = world.view<Paddle, kuge::Transform2D>();

                for (kw::Entity entity : view) {
                    const int side = world.get<Paddle>(entity).side;
                    const bool up = side == 0 ? actions.isDown(Action::P1Up) : actions.isDown(Action::P2Up);
                    const bool down = side == 0 ? actions.isDown(Action::P1Down) : actions.isDown(Action::P2Down);
                    auto& position = world.get<kuge::Transform2D>(entity).position;

                    position.y = clampf(position.y + ((down ? 1.0f : 0.0f) - (up ? 1.0f : 0.0f)) * PADDLE_SPEED * dt, -limit, limit);
                }
                return true;
            }
    };

    class MoveBall : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                auto& match = world.getResource<Match>();
                auto view = world.view<Ball, kuge::Transform2D>();

                for (kw::Entity ball : view) {
                    if (match.serving) {
                        serve(world, ball, match);
                    } else {
                        play(world, ball, match);
                    }
                }
                return true;
            }

        private:
            static void serve(kw::World& world, kw::Entity ball, Match& match)
            {
                if (!world.getResource<kuge::ActionState>().wasPressed(Action::Serve)) {
                    return;
                }
                // Towards the one who did not score; a little up or down, by turns
                const float tick = static_cast<float>(world.getResource<kuge::Time>().tick % 2);
                const float direction = match.server == 0 ? 1.0f : -1.0f;

                world.get<Ball>(ball).velocity = {direction * SERVE_SPEED, (tick == 0.0f ? -0.35f : 0.35f) * SERVE_SPEED};
                match.serving = false;
            }

            static void play(kw::World& world, kw::Entity ball, Match& match)
            {
                const float dt = static_cast<float>(world.getResource<kuge::Time>().dt);
                auto& position = world.get<kuge::Transform2D>(ball).position;
                auto& velocity = world.get<Ball>(ball).velocity;
                const float wall = FIELD_H / 2 - BALL_SIZE / 2;

                position += velocity * dt;
                if (position.y < -wall) {
                    position.y = -wall;
                    velocity.y = std::fabs(velocity.y);
                } else if (position.y > wall) {
                    position.y = wall;
                    velocity.y = -std::fabs(velocity.y);
                }
                auto paddles = world.view<Paddle, kuge::Transform2D>();
                for (kw::Entity paddle : paddles) {
                    bounce(world.get<Paddle>(paddle).side, world.get<kuge::Transform2D>(paddle).position, position, velocity);
                }
                if (position.x < -FIELD_W / 2 - BALL_SIZE) {
                    point(world, ball, match, 1);
                } else if (position.x > FIELD_W / 2 + BALL_SIZE) {
                    point(world, ball, match, 0);
                }
            }

            // The ball hits a paddle only when it is going towards it: it cannot get stuck in it
            static void bounce(int side, kuge::Vec2 paddle, kuge::Vec2& position, kuge::Vec2& velocity)
            {
                const bool towards = side == 0 ? velocity.x < 0.0f : velocity.x > 0.0f;
                const kuge::Rect ball{position.x - BALL_SIZE / 2, position.y - BALL_SIZE / 2, BALL_SIZE, BALL_SIZE};
                const kuge::Rect wood{paddle.x - PADDLE_W / 2, paddle.y - PADDLE_H / 2, PADDLE_W, PADDLE_H};

                if (!towards || !ball.intersects(wood)) {
                    return;
                }
                // The further from the center of the paddle, the steeper it goes
                const float offset = clampf((position.y - paddle.y) / (PADDLE_H / 2), -1.0f, 1.0f);
                const float speed = std::min(velocity.length() * SPEED_UP, MAX_SPEED);
                const float away = side == 0 ? 1.0f : -1.0f;

                velocity = {away * std::cos(offset * MAX_ANGLE) * speed, std::sin(offset * MAX_ANGLE) * speed};
                position.x = paddle.x + away * (PADDLE_W / 2 + BALL_SIZE / 2);
            }

            static void point(kw::World& world, kw::Entity ball, Match& match, int scorer)
            {
                auto& transform = world.get<kuge::Transform2D>(ball);

                if (++match.score[scorer] >= POINTS_TO_WIN) {
                    match.score = {0, 0};
                }
                match.serving = true;
                match.server = scorer;
                transform.position = {0.0f, 0.0f};
                world.get<Ball>(ball).velocity = {0.0f, 0.0f};
                // Back at the center at once, not sliding there from the edge
                world.get<kuge::PreviousTransform2D>(ball).value = transform;
            }
    };

    // The score, drawn with squares: those of the digit are shown, the others hidden
    class ShowScore : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override
            {
                const auto& match = world.getResource<Match>();
                auto view = world.view<Cell, kuge::Sprite>();

                for (kw::Entity entity : view) {
                    const Cell& cell = world.get<Cell>(entity);
                    const int digit = match.score[cell.player] % 10;

                    world.get<kuge::Sprite>(entity).visible = FONT[digit][cell.row][cell.col] == '1';
                }
                return true;
            }
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

    // -- The scene ---------------------------------------------------------------------------
    class PongScene : public kuge::ClientScene
    {
        public:
            using kuge::Scene::world;

            void onEnter(void) override
            {
                world().addResource<Match>();
                world().getResource<kuge::Ref<kuge::IWindow>>()->setTitle(
                    "KUGE Pong - W/S and Up/Down, Space to serve, Esc to quit");
                installClientSystems();
                addSystem(kw::Schedule::Fixed, kuge::stage::Input, std::make_unique<ExitOnRequest>(ctx().engine()));
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<MovePaddles>());
                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<MoveBall>());
                addSystem(kw::Schedule::Fixed, kuge::stage::Late, std::make_unique<ShowScore>());

                makePaddle(0, -PADDLE_X);
                makePaddle(1, PADDLE_X);
                makeBall();
                makeNet();
                makeDigits(0, -110.0f);
                makeDigits(1, 50.0f);
            }

        private:
            kw::Entity makeBox(kuge::Vec2 position, kuge::Vec2 size, kuge::Color color, int layer = 0)
            {
                const kw::Entity entity = world().create();
                kuge::Sprite sprite;

                sprite.size = size;
                sprite.tint = color;
                sprite.layer = layer;
                world().add<kuge::Transform2D>(entity, kuge::Transform2D{position});
                world().add<kuge::Sprite>(entity, sprite);
                return entity;
            }

            // The things that move are smoothed between two ticks
            void smooth(kw::Entity entity)
            {
                world().add<kuge::PreviousTransform2D>(entity, kuge::PreviousTransform2D{world().get<kuge::Transform2D>(entity)});
            }

            void makePaddle(int side, float x)
            {
                const kw::Entity paddle = makeBox({x, 0.0f}, {PADDLE_W, PADDLE_H}, kuge::colors::White, 2);

                world().add<Paddle>(paddle, Paddle{side});
                smooth(paddle);
            }

            void makeBall(void)
            {
                const kw::Entity ball = makeBox({0.0f, 0.0f}, {BALL_SIZE, BALL_SIZE}, kuge::colors::Yellow, 3);

                world().add<Ball>(ball, Ball{});
                smooth(ball);
            }

            void makeNet(void)
            {
                for (int i = 0; i < 15; ++i) {
                    makeBox({0.0f, -FIELD_H / 2 + 20.0f + 40.0f * static_cast<float>(i)}, {6.0f, 24.0f}, {90, 90, 110, 255});
                }
            }

            void makeDigits(int player, float left)
            {
                for (int row = 0; row < 5; ++row) {
                    for (int col = 0; col < 3; ++col) {
                        const kw::Entity cell = makeBox({left + 20.0f * static_cast<float>(col), -260.0f + 20.0f * static_cast<float>(row)},
                            {18.0f, 18.0f}, kuge::colors::White, 1);

                        world().add<Cell>(cell, Cell{player, row, col});
                    }
                }
            }
    };

}
