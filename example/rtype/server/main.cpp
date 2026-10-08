// The server of R-Type: a lobby, and a room for each game. No window.
//
//     rtype_server [port]        (4242 by default; the rooms use the ports after it)
//
// Ctrl+C stops it, and so does "quit" typed in its terminal (a script stops it that way on
// Windows, which cannot send Ctrl+C to another program): every room is closed properly.

#include "GameServer.hpp"
#include "RTypeRoom.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace
{
    // The terminal is read by a thread of its own, which can outlive the server: it reaches the
    // server through this, which the server leaves before it is destroyed
    struct Console
    {
        std::mutex                  mutex;
        kuge::server::GameServer*   server = nullptr;
    };

    void readConsole(std::shared_ptr<Console> console)
    {
        std::string line;

        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line == "quit") {
                std::lock_guard lock(console->mutex);

                if (console->server) {
                    console->server->stop();
                }
                return;
            }
        }
    }
}

int main(int argc, char** argv)
{
    kuge::server::ServerConfig config;

    if (argc > 1) {
        config.lobbyPort = static_cast<std::uint16_t>(std::atoi(argv[1]));
        config.roomPortFirst = static_cast<std::uint16_t>(config.lobbyPort + 1);
    }
    kuge::server::GameServer server(config);

    server.addRoomType<rtype::RTypeRoom>("rtype", {.maxPlayers = 4, .idleTimeout = 30.0});
    auto console = std::make_shared<Console>();

    console->server = &server;
    // Detached: when Ctrl+C stops the server, it may still wait for a line, and the process ends it
    std::thread(readConsole, console).detach();
    const int code = server.run();
    std::lock_guard lock(console->mutex);

    console->server = nullptr;
    return code;
}
