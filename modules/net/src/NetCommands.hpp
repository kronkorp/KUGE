#pragma once

#include "Commands.hpp"
#include "Endpoint.hpp"
#include "NetMeter.hpp"
#include <functional>
#include <memory>

namespace kuge::net
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Commands for a console to try a game against a bad network and to read the numbers of the network, while playing
     *
     *     net_lag <ms>      adds this much to the round trip (half each way)
     *     net_loss <%>      percent of packets lost, each way
     *     net_jitter <ms>   up to this much more or less, at random (so packets pass each other)
     *     net_dup <%>       percent of packets that arrive twice
     *     net_sim off       back to the real network
     *     net_stats         the numbers of the connection (describe(NetReport))
     *
     * They change the SimulatedTransport of the endpoint that `endpoint` gives (the client's; it may change between calls, a
     * game that leaves the room has none: the command says so). The numbers of `net_stats` are those of a NetMeter that the
     * commands keep. A game that wants a constant condition sets EndpointConfig::simulate instead.
     *
     * @param connection  The connection whose round trip net_stats reports (CLIENT_CONNECTION for a client)
     */
    ////////////////////////////////////////////////////////////////////////////
    void registerNetCommands(Commands& commands, std::function<Endpoint*(void)> endpoint, ConnectionId connection = CLIENT_CONNECTION);

}
