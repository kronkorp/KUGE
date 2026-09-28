// The server of R-Type: a lobby, and a room for each game. No window.
//
//     rtype_server [port]        (4242 by default; the rooms use the ports after it)
//
// Ctrl+C stops it: every room is closed properly.

#include "GameServer.hpp"
#include "RTypeRoom.hpp"
#include <cstdlib>

int main(int argc, char** argv)
{
    kuge::server::ServerConfig config;

    if (argc > 1) {
        config.lobbyPort = static_cast<std::uint16_t>(std::atoi(argv[1]));
        config.roomPortFirst = static_cast<std::uint16_t>(config.lobbyPort + 1);
    }
    kuge::server::GameServer server(config);

    server.addRoomType<rtype::RTypeRoom>("rtype", {.maxPlayers = 4, .idleTimeout = 30.0});
    return server.run();
}
