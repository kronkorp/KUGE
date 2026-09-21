#pragma once

#include "Matchmaking.hpp"
#include "SceneContext.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace kuge::server
{

    //! What a kind of room is like (see GameServer::addRoomType)
    struct RoomTypeConfig
    {
        std::size_t maxPlayers  = 8;
        double      idleTimeout = 30.0;                       //!< A room with no player for this long closes (seconds)
        RunPolicy   policy      = RunPolicy::Dedicated;       //!< Where the rooms run: a thread each, or the workers
    };

    //! How the server is reached and what it is allowed to do
    enum class Transport : std::uint8_t {
        Sockets,    //!< The real network: a lobby on a well known port, rooms on ports of a range (UDP)
        Loopback,   //!< In the process, by name: for tests, or a game that hosts its own server
    };

    struct ServerConfig
    {
        Transport               transport      = Transport::Sockets;

        // The lobby, where clients arrive
        net::Protocol           lobbyProtocol  = net::Protocol::Tcp;   //!< TCP: the connection is kept, and its loss is felt at once
        std::uint16_t           lobbyPort      = 4242;
        std::string             lobbyName      = "lobby";              //!< Loopback
        net::LoopbackNetwork*   loopback       = nullptr;              //!< Loopback: the network (the process-wide one if null)

        // The rooms
        std::uint16_t           roomPortFirst  = 4300;                 //!< Sockets: each room takes a port of this range (UDP)
        std::uint16_t           roomPortCount  = 64;
        std::string             roomAddress;                           //!< Sockets: the host that clients are told to use (empty: the one they used for the lobby)
        std::size_t             maxRooms       = 16;

        std::uint32_t           tickRate       = 60;
        std::uint32_t           workers        = 0;                    //!< For rooms with RunPolicy::Pooled (0: one per CPU)

        double                  tokenTtl       = 10.0;                 //!< A client has this long to reach the room it was sent to
        double                  helloTimeout   = 5.0;                  //!< A connection that does not say hello in this time is dropped
        double                  linger         = 0.3;                  //!< A closing room waits this long for its last messages to leave

        net::EndpointConfig     endpoint;                              //!< Timeouts, and the clock, for the lobby and the rooms

        double now(void) const
        {
            return endpoint.clock ? endpoint.clock() : std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    };

    //! What a room is given to start with (by the lobby)
    struct RoomInit
    {
        std::uint32_t   roomId = 0;
        std::string     roomType;
        RoomTypeConfig  type;
        ServerConfig    server;
        std::uint16_t   port = 0;          //!< Sockets
        std::string     address;           //!< Loopback: its name
    };

    //! What goes on in the server, for whoever watches (readable from any thread)
    struct ServerStats
    {
        std::atomic<bool>         lobbyOpen{false};  //!< The lobby listens
        std::atomic<std::size_t>  rooms{0};          //!< Rooms that run
        std::atomic<std::size_t>  players{0};        //!< Players in a room or on their way to one
        std::atomic<std::size_t>  lobbyClients{0};   //!< Clients connected to the lobby
        std::atomic<std::size_t>  roomsMade{0};
        std::atomic<std::size_t>  joins{0};
        std::atomic<std::size_t>  refused{0};
    };

    // -- What lobby and rooms say to each other (scene messages: no network) ----------------------
    namespace detail
    {
        struct ExpectPlayer  { std::uint32_t playerId; std::string name; std::uint64_t token; };
        struct KickPlayer    { std::uint32_t playerId; net::RoomEnd reason; };
        struct RoomReady     { std::uint32_t roomId; };
        struct RoomClosing   { std::uint32_t roomId; };      //!< The game ended: no more players, and the ones it has may join again
        struct RoomFailed    { std::uint32_t roomId; std::string why; };
        struct PlayerJoined  { std::uint32_t roomId; std::uint32_t playerId; };
        struct PlayerLeft    { std::uint32_t roomId; std::uint32_t playerId; };
        struct RoomEnded     { std::uint32_t roomId; net::RoomEnd reason; };
    }

}
