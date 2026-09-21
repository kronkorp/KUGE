// The R-Type client: a window on a game that runs on a server.
//
//     rtype_client [--host H] [--port P] [--name N]
//         arrows or WASD move the ship, Space fires, Esc quits
//     rtype_client --frames N [--screenshot file.ppm]
//         a scripted pilot flies for N frames, as fast as it can (no waiting), and the picture of
//         the last frame is kept. Exit code 0 if it joined a game and saw it.

#include "RTypeClient.hpp"
#include "Script.hpp"
#include "backend/SdlBackend.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int argc, char** argv)
{
    rtype::ClientOptions options;
    int frames = 0;
    const char* screenshot = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--host") && i + 1 < argc) {
            options.host = argv[++i];
        } else if (!std::strcmp(argv[i], "--port") && i + 1 < argc) {
            options.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) {
            options.name = argv[++i];
        } else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot = argv[++i];
        } else {
            Logger::logger().error("usage: rtype_client [--host H] [--port P] [--name N] [--frames N [--screenshot f.ppm]]");
            return 2;
        }
    }
    try {
        kuge::WindowConfig window;

        window.title = "KUGE R-Type";
        window.width = 960;
        window.height = 540;
        kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
        auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window), kuge::ClientConfig{kuge::Color{6, 6, 16, 255}});
        auto report = std::make_shared<rtype::ClientReport>();

        rtype::bindDefaults(client.input());
        engine.scenes().change<rtype::RTypeScene>(options, report);
        if (frames == 0) {
            return engine.run();
        }
        // Scripted: as fast as it goes, but the server lives in real time, so a frame waits for its time
        auto next = std::chrono::steady_clock::now();

        for (int frame = 0; frame < frames; ++frame) {
            rtype::scriptedPilot(client.input(), frame);
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

            if (!image || !rtype::writePpm(screenshot, *image)) {
                Logger::logger().error("cannot keep the screenshot");
                return 1;
            }
        }
        Logger::logger().info("report: joined={} games={} ended={} ships={} bullets={}(max {}) enemies={}(max {}) score={} health={} corrections={} of {} pending={} snapshots={}",
            report->joined, report->games, report->gamesEnded, report->ships, report->bullets, report->mostBullets, report->enemies, report->mostEnemies,
            report->score, report->health, report->prediction.corrections, report->prediction.reconciliations, report->prediction.pending, report->replication.snapshotsApplied);
        return report->joined && report->mostShips >= 1 ? 0 : 3;
    } catch (const std::exception& error) {
        Logger::logger().error("{}", error.what());
        return 1;
    }
}
