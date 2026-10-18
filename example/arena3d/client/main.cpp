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
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    arena::ClientOptions options;
    int frames = 0;
    const char* screenshot = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--host") && i + 1 < argc) {
            options.host = argv[++i];
        } else if (!std::strcmp(argv[i], "--port") && i + 1 < argc) {
            options.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) {
            options.name = argv[++i];
        } else if (!std::strcmp(argv[i], "--assets") && i + 1 < argc) {
            options.assets = argv[++i];
        } else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot = argv[++i];
        } else {
            Logger::logger().error("usage: arena3d_client [--host H] [--port P] [--name N] [--assets DIR] [--frames N [--screenshot f.ppm]]");
            return 2;
        }
    }
    try {
        kuge::WindowConfig window;

        window.title = "KUGE Arena";
        window.width = 640;
        window.height = 360;
        options.captureMouse = frames == 0;
        kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
        auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window));
        auto report = std::make_shared<arena::ClientReport>();

        engine.addModule<kuge::Render3DModule>();
        arena::bindDefaults(client.input());
        engine.scenes().change<arena::ArenaScene>(options, report);
        if (frames == 0) {
            return engine.run();
        }
        // Scripted: as fast as it goes, but the server lives in real time, so a frame waits for its time
        auto next = std::chrono::steady_clock::now();

        for (int frame = 0; frame < frames; ++frame) {
            arena::scriptedPlayer(client.input(), frame);
            if (frame == frames - 1) {
                client.requestScreenshot();
            }
            if (!engine.step(1.0 / 60.0)) {
                break;
            }
            next += std::chrono::microseconds(16667);      // (the schedule is absolute: the frames do not drift from the clock of the server)
            std::this_thread::sleep_until(next);
        }
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
        Logger::logger().info("report: joined={} games={} ended={} players={} crates={} bullets={}(max {}) health={} clips=[{}] corrections={} of {} pending={} snapshots={}",
            report->joined, report->games, report->gamesEnded, report->players, report->crates, report->bullets, report->mostBullets,
            report->health, clips, report->prediction.corrections, report->prediction.reconciliations, report->prediction.pending,
            report->replication.snapshotsApplied);
        return arena::scriptPassed(*report) ? 0 : 3;
    } catch (const std::exception& error) {
        Logger::logger().error("{}", error.what());
        return 1;
    }
}
