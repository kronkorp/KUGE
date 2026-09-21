extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Serializer.hpp"
#include "client_fixture.hpp"
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    enum class Action : std::uint8_t { Jump };

    // Keeps what the simulation read at each tick
    struct Seen {
        std::vector<kuge::ActionState> states;
    };

    class ReadActions : public kw::ISystem
    {
        public:
            explicit ReadActions(Seen& seen) : m_seen(seen) {}

            bool handle(kw::World& world) override
            {
                m_seen.states.push_back(world.getResource<kuge::ActionState>());
                return true;
            }

        private:
            Seen& m_seen;
    };
}

Test(client, needs_a_windowed_engine)
{
    kuge::Engine headless;
    auto dummy = kuge::makeDummyBackend();
    bool refused = false;

    try { headless.addModule<kuge::ClientModule>(std::move(dummy.backend)); } catch (const std::logic_error&) { refused = true; }
    Assert(refused, "a headless engine has no use for a window");
    Assert(headless.module<kuge::ClientModule>() == nullptr, "and it was not added");
}

Test(client, needs_a_whole_backend)
{
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});
    kuge::Backend empty;
    bool refused = false;

    try { engine.addModule<kuge::ClientModule>(std::move(empty)); } catch (const std::invalid_argument&) { refused = true; }
    Assert(refused, "no window, no inputs, no renderer");
}

Test(client, clears_and_presents)
{
    Fixture fx;

    AssertEq(fx.dummy.renderer->presented(), 1, "the first loop showed a frame");
    Assert(fx.dummy.renderer->clearColor() == kuge::Color(24, 24, 32, 255), "cleared with the default color");
    fx.frame();
    fx.frame();
    AssertEq(fx.dummy.renderer->presented(), 3, "one frame per loop");
}

Test(client, clear_color_is_a_setting)
{
    auto dummy = kuge::makeDummyBackend();
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});

    engine.addModule<kuge::ClientModule>(std::move(dummy.backend), kuge::ClientConfig{kuge::colors::Red});
    engine.scenes().change<TestScene>();
    engine.step(0.0);
    Assert(dummy.renderer->clearColor() == kuge::colors::Red, "the color of the game");
}

Test(client, scenes_get_the_services)
{
    Fixture fx;
    auto& world = fx.scene->world();

    Assert(&world.getResource<kuge::Ref<kuge::IWindow>>().get() == &fx.client->window(), "window");
    Assert(&world.getResource<kuge::Ref<kuge::IRenderer2D>>().get() == &fx.client->renderer(), "renderer");
    Assert(&world.getResource<kuge::Ref<kuge::InputMap>>().get() == &fx.client->input(), "inputs");
    Assert(&world.getResource<kuge::Ref<kuge::AssetManager<kuge::Texture>>>().get() == &fx.client->textures(), "assets");
    Assert(world.getResource<kuge::Camera2D>().zoom == 1.0f, "a camera");
    Assert(world.getResource<kuge::WhitePixel>().texture != nullptr, "a white pixel");
}

Test(client, inputs_become_actions)
{
    Seen seen;
    Fixture fx([&seen](TestScene& scene) {
        scene.addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<ReadActions>(seen));
    });

    fx.client->input().bind(Action::Jump, kuge::Key::Space);
    fx.dummy.input->push(kuge::KeyEvent{kuge::Key::Space, true});
    fx.engine.step(1.0 / 60.0);
    AssertEq(seen.states.size(), 1, "one tick");
    Assert(seen.states[0].isDown(Action::Jump) && seen.states[0].wasPressed(Action::Jump), "the key became the action");
    AssertEq(seen.states[0].tick, 0, "of tick 0");
    fx.dummy.input->push(kuge::KeyEvent{kuge::Key::Space, false});
    fx.engine.step(1.0 / 60.0);
    Assert(!seen.states[1].isDown(Action::Jump) && seen.states[1].wasReleased(Action::Jump), "and back");
    AssertEq(seen.states[1].tick, 1, "of tick 1");
}

Test(client, press_counts_once_catching_up)
{
    Seen seen;
    Fixture fx([&seen](TestScene& scene) {
        scene.addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<ReadActions>(seen));
    });

    fx.client->input().bind(Action::Jump, kuge::Key::Space);
    fx.dummy.input->push(kuge::KeyEvent{kuge::Key::Space, true});
    fx.engine.step(3.0 / 60.0);
    AssertEq(seen.states.size(), 3, "3 ticks in one loop");
    Assert(seen.states[0].wasPressed(Action::Jump), "the first one sees the press");
    Assert(!seen.states[1].wasPressed(Action::Jump) && seen.states[1].isDown(Action::Jump), "the others only see it held");
    Assert(!seen.states[2].wasPressed(Action::Jump) && seen.states[2].isDown(Action::Jump), "still held");
}

Test(client, closing_the_window_stops)
{
    Seen seen;
    Fixture fx([&seen](TestScene& scene) {
        scene.addSystem(kw::Schedule::Fixed, kuge::stage::Simulation, std::make_unique<ReadActions>(seen));
    });

    fx.dummy.input->push(kuge::QuitEvent{});
    AssertEq(fx.engine.step(1.0 / 60.0), false, "the engine is done");
    AssertEq(seen.states.size(), 0, "without another tick");
}

Test(client, screenshot_is_one_shot)
{
    Fixture fx;

    Assert(!fx.client->takeScreenshot().has_value(), "none unless asked");
    fx.client->requestScreenshot();
    fx.frame();
    Assert(fx.client->takeScreenshot().has_value(), "taken during the frame that followed");
    Assert(!fx.client->takeScreenshot().has_value(), "given once");
    fx.frame();
    Assert(!fx.client->takeScreenshot().has_value(), "and not for the next frames");
}

Test(client, textures_of_files_are_shared)
{
    const auto path = std::filesystem::temp_directory_path() / ("kuge_test_" + std::to_string(::getpid()) + "_client.bmp");
    Fixture fx;
    kuge::ByteWriter bmp;

    // The smallest picture: 1x1, 24 bits
    bmp.write<char>('B'); bmp.write<char>('M'); bmp.write<std::uint32_t>(58); bmp.write<std::uint32_t>(0);
    bmp.write<std::uint32_t>(54); bmp.write<std::uint32_t>(40); bmp.write<std::int32_t>(1); bmp.write<std::int32_t>(1);
    bmp.write<std::uint16_t>(1); bmp.write<std::uint16_t>(24); bmp.write<std::uint32_t>(0); bmp.write<std::uint32_t>(4);
    bmp.write<std::int32_t>(0); bmp.write<std::int32_t>(0); bmp.write<std::uint32_t>(0); bmp.write<std::uint32_t>(0);
    bmp.write<std::uint32_t>(0x00FF00FFu);
    kuge::writeFile(path, bmp.bytes());

    const std::size_t before = fx.dummy.renderer->textureCount();
    auto a = fx.client->textures().load(path);
    auto b = fx.client->textures().load(path);
    Assert(a == b, "loaded once, shared");
    AssertEq(fx.dummy.renderer->textureCount(), before + 1, "one texture in the renderer");
    a.reset();
    b.reset();
    AssertEq(fx.dummy.renderer->textureCount(), before, "given back when nobody holds it");
    std::filesystem::remove(path);
}
