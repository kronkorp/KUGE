// R-Type with its own server: the room and the window in one process.
//
//     rtype_host                          plays; the room is on a thread of its own
//     rtype_host --frames N [--screenshot f.ppm]      scripted, like rtype_client
//
// The same room as rtype_server, and the same scene as rtype_client: only the way they reach each other
// changes (a loopback network of the process, instead of sockets). It is the check that the separation
// of the client and the server is real: nothing of either knows where the other is.

#include "GameServer.hpp"
#include "RTypeClient.hpp"
#include "RTypeRoom.hpp"
#include "Script.hpp"
#include "backend/SdlBackend.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include <cstdlib>
#include <cstring>
#include <thread>

int main(int argc, char** argv)
{
    int frames = 0;
    const char* screenshot = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot = argv[++i];
        } else {
            Logger::logger().error("usage: rtype_host [--frames N [--screenshot f.ppm]]");
            return 2;
        }
    }
    try {
        // The server: an engine with no window, on its own thread, reached by name
        kuge::net::LoopbackNetwork network;
        kuge::server::ServerConfig config;

        config.transport = kuge::server::Transport::Loopback;
        config.loopback = &network;
        kuge::server::GameServer server(config);

        server.addRoomType<rtype::RTypeRoom>("rtype", {.maxPlayers = 4, .idleTimeout = 30.0});
        std::thread serverThread([&server] { server.run(); });

        while (!server.stats().lobbyOpen) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        int status = 0;
        {
            kuge::WindowConfig window;

            window.title = "KUGE R-Type (host)";
            window.width = 960;
            window.height = 540;
            kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
            auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window), kuge::ClientConfig{kuge::Color{6, 6, 16, 255}});
            auto report = std::make_shared<rtype::ClientReport>();
            rtype::ClientOptions options;

            options.sockets = false;
            options.network = &network;
            rtype::bindDefaults(client.input());
            engine.scenes().change<rtype::RTypeScene>(options, report);
            if (frames == 0) {
                engine.run();
            } else {
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
                        status = 1;
                    }
                }
                Logger::logger().info("report: joined={} games={} ended={} ships={} bullets={}(max {}) enemies={}(max {}) score={} corrections={}",
                    report->joined, report->games, report->gamesEnded, report->ships, report->bullets, report->mostBullets,
                    report->enemies, report->mostEnemies, report->score, report->prediction.corrections);
                if (status == 0 && !(report->joined && report->mostShips >= 1)) {
                    status = 3;
                }
            }
        }
        server.stop();
        serverThread.join();
        return status;
    } catch (const std::exception& error) {
        Logger::logger().error("{}", error.what());
        return 1;
    }
}
