// Arena with its own server: the room and the window in one process.
//
//     arena3d_host                          plays; the room is on a thread of its own
//     arena3d_host --frames N [--screenshot f.ppm]      scripted, like arena3d_client
//
// The same room as arena3d_server, and the same scene as arena3d_client: only the way they reach each other
// changes (a loopback network of the process, instead of sockets). It is how a game plays alone.

#include "ArenaClient.hpp"
#include "ArenaRoom.hpp"
#include "Engine.hpp"
#include "GameServer.hpp"
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
    int frames = 0;
    const char* screenshot = nullptr;
    std::filesystem::path assets = ARENA_ASSETS_DIR;
    float sensitivity = 0.075f;
    double daySeconds = 150.0;
    bool firstPerson = true;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--sensitivity") && i + 1 < argc) {
            sensitivity = static_cast<float>(std::atof(argv[++i]));
        } else if (!std::strcmp(argv[i], "--day") && i + 1 < argc) {
            daySeconds = std::atof(argv[++i]);
        } else if (!std::strcmp(argv[i], "--third-person")) {
            firstPerson = false;
        } else if (!std::strcmp(argv[i], "--assets") && i + 1 < argc) {
            assets = argv[++i];
        } else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot = argv[++i];
        } else {
            Logger::logger().error("usage: arena3d_host [--sensitivity DEG] [--day SECONDS] [--third-person] [--assets DIR] [--frames N [--screenshot f.ppm]]");
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

        server.addRoomType<arena::ArenaRoom>("arena", {.maxPlayers = 4, .idleTimeout = 30.0});
        std::thread serverThread([&server] { server.run(); });

        while (!server.stats().lobbyOpen) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        int status = 0;
        {
            kuge::WindowConfig window;

            window.title = "KUGE Arena (host)";
            window.width = frames > 0 ? 640 : 1280;      // (a script has a small one: it is drawn by the CPU)
            window.height = frames > 0 ? 360 : 720;
            window.resizable = true;
            kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed, .tickRate = 60});
            auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(window));
            auto report = std::make_shared<arena::ClientReport>();
            arena::ClientOptions options;

            engine.addModule<kuge::Render3DModule>();
            options.sockets = false;
            options.network = &network;
                options.assets = assets;
            options.sensitivity = sensitivity;
            options.daySeconds = daySeconds;
            options.firstPerson = firstPerson;
            arena::bindDefaults(client.input());
            engine.scenes().change<arena::ArenaScene>(options, report);
            if (frames == 0) {
                engine.run();
            } else {
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
                        status = 1;
                    }
                }
                std::string clips;

                for (const auto& clip : report->clips) {
                    clips += clip + " ";
                }
                Logger::logger().info("report: joined={} games={} ended={} players={} crates={} bullets={}(max {}) health={} clips=[{}] corrections={}",
                    report->joined, report->games, report->gamesEnded, report->players, report->crates, report->bullets, report->mostBullets,
                    report->health, clips, report->prediction.corrections);
                if (status == 0 && !arena::scriptPassed(*report)) {
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
