extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ClientCommands.hpp"
#include "DebugDraw.hpp"
#include "Physics.hpp"
#include "backend/SdlBackend.hpp"
#include "client_fixture.hpp"
#include "ui/DebugOverlay.hpp"
#include "ui_fixture.hpp"
#include <cmath>
#include <cstdlib>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    using Call = kuge::DummyRenderer::Call;

    struct Debug
    {
        kuge::Cvars       cvars;
        kuge::Commands    commands{cvars};
        kuge::DebugCvars  flags = kuge::registerDebugCvars(cvars);
        Fixture           fx;

        Debug() : fx([this](TestScene& scene) {
                kuge::installPhysics(scene.setup(), {.gravity = {0.0f, 0.0f}});
                kuge::installDebug(scene.setup(), flags);
                auto& w = scene.world();
                const kw::Entity wall = w.create();
                const kw::Entity mover = w.create();
                const kw::Entity zone = w.create();
                kuge::Sprite sprite;

                w.add<kuge::Transform2D>(wall, kuge::Transform2D{{0.0f, 0.0f}});
                w.add<kuge::Collider>(wall, kuge::Collider::box(20.0f, 20.0f));
                w.add<kuge::Transform2D>(mover, kuge::Transform2D{{100.0f, 0.0f}});
                w.add<kuge::Collider>(mover, kuge::Collider::box(10.0f, 10.0f));
                w.add<kuge::Body>(mover, kuge::Body{kuge::Body::Type::Kinematic});
                w.add<kuge::Transform2D>(zone, kuge::Transform2D{{-100.0f, 0.0f}});
                w.add<kuge::Collider>(zone, kuge::Collider::circle(10.0f).asTrigger());
                sprite.size = {30.0f, 10.0f};
                w.add<kuge::Sprite>(mover, sprite);
            }) {}

        std::vector<Call> strokes(void)
        {
            std::vector<Call> found;

            for (const Call& call : fx.frame(1.0 / 60.0)) {
                if (call.kind == Call::Kind::Stroke) {
                    found.push_back(call);
                }
            }
            return found;
        }
    };

    bool near(const kuge::Rect& a, const kuge::Rect& b) { return std::fabs(a.x - b.x) < 0.01f && std::fabs(a.y - b.y) < 0.01f && std::fabs(a.w - b.w) < 0.01f && std::fabs(a.h - b.h) < 0.01f; }
}

Test(debug, colliders_only_when_asked)
{
    Debug debug;

    debug.fx.frame(1.0 / 60.0);
    AssertEq(debug.strokes().size(), 0u, "off: nothing is drawn");
    Assert(debug.fx.scene->world().getResource<kuge::DebugDraw>().shapes.empty(), "and nothing was computed");
    debug.commands.run("debug_colliders on");
    debug.fx.frame(1.0 / 60.0);
    const auto strokes = debug.strokes();

    AssertEq(strokes.size(), 3u, "on: a wall, a mover and a trigger, got %zu", strokes.size());
    bool wall = false, mover = false, zone = false;

    for (const Call& call : strokes) {
        // The screen is 640 x 480: the world (0, 0) is at (320, 240)
        wall = wall || (near(call.destination, {310.0f, 230.0f, 20.0f, 20.0f}) && call.color == kuge::Color{80, 140, 255, 255});
        mover = mover || (near(call.destination, {415.0f, 235.0f, 10.0f, 10.0f}) && call.color == kuge::Color{90, 230, 110, 255});
        zone = zone || (near(call.destination, {210.0f, 230.0f, 20.0f, 20.0f}) && call.color == kuge::Color{250, 220, 70, 255});
    }
    Assert(wall, "the static collider, in blue, where it is");
    Assert(mover, "the moving one, in green");
    Assert(zone, "the trigger, in yellow (a circle by its box)");
    debug.commands.run("debug_colliders off");
    debug.fx.frame(1.0 / 60.0);
    AssertEq(debug.strokes().size(), 0u, "off again");
}

Test(debug, sprite_boxes_and_camera)
{
    Debug debug;

    debug.commands.run("debug_bounds true");
    debug.fx.frame(1.0 / 60.0);
    auto strokes = debug.strokes();

    AssertEq(strokes.size(), 1u, "the one sprite");
    Assert(near(strokes[0].destination, {405.0f, 235.0f, 30.0f, 10.0f}), "its box");
    debug.commands.run("debug_bounds off");
    debug.commands.run("debug_camera on");
    debug.fx.frame(1.0 / 60.0);
    strokes = debug.strokes();
    AssertEq(strokes.size(), 1u, "the edge of the camera");
    Assert(near(strokes[0].destination, {0.0f, 0.0f, 640.0f, 480.0f}), "all the window here");
}

Test(debug, the_screenshot_command)
{
    Debug debug;
    const auto file = std::filesystem::temp_directory_path() / "kuge_debug_shot.ppm";

    kuge::registerClientCommands(*debug.fx.client, debug.commands);
    std::filesystem::remove(file);
    Assert(debug.commands.run("screenshot " + file.string()).find(file.string()) != std::string::npos, "it says where");
    debug.fx.frame(1.0 / 60.0);
    Assert(!std::filesystem::exists(file), "the dummy backend cannot read its pixels: no file (a warning in the log)");
}

Test(debug, the_screenshot_of_sdl)
{
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_RENDER_DRIVER", "software", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    kuge::WindowConfig window;

    window.width = 64;
    window.height = 48;
    window.vsync = false;
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});
    auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window), kuge::ClientConfig{kuge::Color{10, 20, 30, 255}});
    kuge::Cvars cvars;
    kuge::Commands commands(cvars);
    const auto file = std::filesystem::temp_directory_path() / "kuge_sdl_shot.ppm";

    kuge::registerClientCommands(client, commands);
    engine.scenes().change<TestScene>(std::function<void(TestScene&)>{});
    engine.step(0.0);
    std::filesystem::remove(file);
    commands.run("screenshot " + file.string());
    engine.step(1.0 / 60.0);
    std::ifstream in(file, std::ios::binary);
    std::string header(11, '\0');

    in.read(header.data(), 11);
    Assert(in && header.substr(0, 9) == "P6\n64 48\n", "a PPM of the window: '%s'", header.c_str());
    unsigned char pixel[3];

    in.seekg(0, std::ios::end);
    AssertEq(static_cast<long long>(in.tellg()), 13 + 64 * 48 * 3, "the size of the picture");
    in.seekg(13);
    in.read(reinterpret_cast<char*>(pixel), 3);
    Assert(pixel[0] == 10 && pixel[1] == 20 && pixel[2] == 30, "with the colour of the background");
    std::filesystem::remove(file);
}

Test(debug, the_overlay_shows_numbers)
{
    FontFile font;
    std::unique_ptr<kuge::DebugOverlay> overlay;
    Fixture fx([&](TestScene& scene) {
        withUi(scene, font, [&](kw::World& world) { overlay = std::make_unique<kuge::DebugOverlay>(world, kuge::DebugOverlay::Config{.bars = 10, .systems = 2, .rtts = 5}); });
    });
    kuge::FrameStats stats;
    kuge::NetReport net;
    kw::World& world = fx.scene->world();

    for (int i = 0; i < 20; ++i) {
        stats.push(i == 19 ? 0.2 : 0.016, 0.001, 0.004);
    }
    net.rttSeconds = 0.06;
    net.rttLow = 0.04;
    net.rttHigh = 0.09;
    overlay->update(world, stats, &net);
    Assert(overlay->lines(world)[0].empty(), "hidden: nothing is worked out");
    overlay->setVisible(world, true);
    overlay->update(world, stats, &net);
    const auto lines = overlay->lines(world);

    Assert(lines[0].find("fps") != std::string::npos && lines[0].find("[SLOW]") != std::string::npos, "the frames, and the worst one is called slow: %s", lines[0].c_str());
    Assert(lines[1].find("tick 1.00 ms") != std::string::npos, "the tick: %s", lines[1].c_str());
    Assert(lines[2].find("net  rtt 60 ms (40-90)") == 0, "the net line: %s", lines[2].c_str());
    kuge::Gauges::global().set("audio.voices", 3);
    overlay->update(world, stats, nullptr);
    Assert(overlay->lines(world)[2].empty(), "no net, no net line");
    const auto after = overlay->lines(world);

    Assert(std::any_of(after.begin(), after.end(), [](const std::string& l) { return l == "audio.voices = 3"; }), "the gauges");
    Assert(std::any_of(after.begin(), after.end(), [](const std::string& l) { return l.find("profiling is off") != std::string::npos; }), "and how to turn the profiler on");
}
