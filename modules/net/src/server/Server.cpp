#include "Server.hpp"
extern "C" {
    #include "kronknet/server/server.h"
}

kuge::Network::Server::Server(
    knPort port,
    knFlags flags
)
{
    m_server = knServer_create(port, flags);
    if (!m_server) {
        // TODO: throw
    }
}

kuge::Network::Server::~Server()
{
    if (m_server) {
        knServer_destroy(m_server);
    }
}
