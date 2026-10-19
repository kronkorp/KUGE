// The Arena client: a window on a game that runs on a server.
//
//     arena3d_client [--host H] [--port P] [--name N]
//         W A S D or the arrows walk, the mouse turns the view, Space jumps, the left button (or F) fires, V
//         looks from the eyes or from behind
//     arena3d_client --frames N [--screenshot file.ppm]
//         a scripted player walks for N frames, as fast as it can (no waiting), and the picture of
//         the last frame is kept. Exit code 0 if it joined a game, ran, jumped and shot (its character played
//         each clip) and its bullets were seen.

#include "ArenaClient.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include "Render3DModule.hpp"
#include "Script.hpp"
#include "backend/SdlBackend.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    arena::ClientOptions options;
    int frames = 0;
    int width = 0;
    int height = 0;
    const char* screenshot = nullptr;
    kuge::Rendering rendering = kuge::Rendering::Auto;      // the GPU if there is one

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--host") && i + 1 < argc) {
            options.host = argv[++i];
        } else if (!std::strcmp(argv[i], "--port") && i + 1 < argc) {
            options.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) {
            options.name = argv[++i];
        } else if (!std::strcmp(argv[i], "--sensitivity") && i + 1 < argc) {
            options.sensitivity = static_cast<float>(std::atof(argv[++i]));
        } else if (!std::strcmp(argv[i], "--day") && i + 1 < argc) {
            options.daySeconds = std::atof(argv[++i]);
        } else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) {
            std::sscanf(argv[++i], "%dx%d", &width, &height);
        } else if (!std::strcmp(argv[i], "--software")) {
            rendering = kuge::Rendering::Software;
        } else if (!std::strcmp(argv[i], "--opengl")) {
            rendering = kuge::Rendering::OpenGL;
        } else if (!std::strcmp(argv[i], "--third-person")) {
            options.firstPerson = false;
        } else if (!std::strcmp(argv[i], "--assets") && i + 1 < argc) {
            options.assets = argv[++i];
        } else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot = argv[++i];
        } else {
            Logger::logger().error("usage: arena3d_client [--host H] [--port P] [--name N] [--sensitivity DEG] [--day SECONDS] [--size WxH] [--software | --opengl] [--third-person] [--assets DIR] [--frames N [--screenshot f.ppm]]");
            return 2;
        }
    }
    try {
        kuge::WindowConfig window;

        window.title = "KUGE Arena";
        window.width = width > 0 ? width : (frames > 0 ? 640 : 1280);      // (a script has a small one: it may be drawn by the CPU)
        window.height = height > 0 ? height : (frames > 0 ? 360 : 720);
        window.resizable = true;
        kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
        window.openGL = rendering != kuge::Rendering::Software;
        auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window));
        auto report = std::make_shared<arena::ClientReport>();

        engine.addModule<kuge::Render3DModule>(client, kuge::Render3DConfig{.rendering = rendering});
        arena::bindDefaults(client.input());
        engine.scenes().change<arena::ArenaScene>(options, report);
        if (frames == 0) {
            return engine.run();
        }
        // Scripted: in real time (the server lives in real time), for as many frames as asked at 60 a second
        arena::playScript(engine, client, frames);
        if (screenshot) {
            const auto image = client.takeScreenshot();

            if (!image || !arena::writePpm(screenshot, *image)) {
                Logger::logger().error("cannot keep the screenshot");
                return 1;
            }
        }
        std::string clips;

        for (const auto& clip : report->clips) {
            clips += clip + " ";
        }
        Logger::logger().info("report: joined={} frames={} games={} ended={} players={} crates={} bullets={}(max {}) health={} clips=[{}] corrections={} of {} pending={} snapshots={}",
            report->joined, report->frames, report->games, report->gamesEnded, report->players, report->crates, report->bullets, report->mostBullets,
            report->health, clips, report->prediction.corrections, report->prediction.reconciliations, report->prediction.pending,
            report->replication.snapshotsApplied);
        return arena::scriptPassed(*report) ? 0 : 3;
    } catch (const kuge::Render3DError& error) {
        Logger::logger().error("{}", error.what());
        return rendering == kuge::Rendering::OpenGL ? 77 : 1;      // 77: "skipped" for ctest (no GPU here)
    } catch (const kuge::BackendError& error) {
        Logger::logger().error("{}", error.what());
        return rendering == kuge::Rendering::OpenGL ? 77 : 1;      // (a machine that cannot even show the offscreen window)
    } catch (const std::exception& error) {
        Logger::logger().error("{}", error.what());
        return 1;
    }
}
