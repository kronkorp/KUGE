#pragma once

#include "Matchmaking.hpp"
#include "SceneContext.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kuge::server
{

    //! What a kind of room is like (see GameServer::addRoomType)
    struct RoomTypeConfig
    {
        std::size_t maxPlayers  = 8;
        double      idleTimeout = 30.0;                       //!< A room with no player for this long closes (seconds)
        RunPolicy   policy      = RunPolicy::Dedicated;       //!< Where the rooms run: a thread each, or the workers
        //! A player whose connection is lost (a timeout; not one who leaves, is sent away, or when the game ends) keeps its place this long
        //! (seconds) and can come back with MatchmakingClient::rejoin(), as the same player with the same network id. The room is told
        //! `onPlayerAway` at once and `onPlayerLeft` when the time is up. 0: a lost connection is a player who left (as always).
        double      reconnectSeconds = 0.0;
    };

    //! How the server is reached and what it is allowed to do
    enum class Transport : std::uint8_t {
        Sockets,    //!< The real network: a lobby on a well known port, rooms on ports of a range (UDP)
        Loopback,   //!< In the process, by name: for tests, or a game that hosts its own server
    };

    //! A player who asks for any room of a kind (JoinRoom), as ServerConfig::roomPicker sees it
    struct JoinRequest
    {
        std::string   roomType;
        std::string   playerName;
        std::string   options;           //!< JoinRoom::options, as the client sent them
        std::uint32_t playerId = 0;      //!< What ServerConfig::authenticate answered (0 if the server does not check)
        std::string   address;           //!< Where the player comes from, without the port
        double        waited = 0.0;      //!< Seconds since the request came (it grows while the picker says wait)
    };

    //! What ServerConfig::roomPicker decides for a JoinRequest
    struct RoomChoice
    {
        enum class Kind : std::uint8_t { Join, Make, Wait };

        Kind          kind = Kind::Wait;
        std::uint32_t roomId = 0;        //!< Join: one of the rooms the picker was given
        std::string   options;           //!< Make: the options of the new room (a validRoomOptions() text), in its RoomInfo

        static RoomChoice join(std::uint32_t roomId) { return {Kind::Join, roomId, {}}; }
        static RoomChoice make(std::string options = "") { return {Kind::Make, 0, std::move(options)}; }
        static RoomChoice wait(void) { return {}; }
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
        double                  closeDelay     = 0.1;                  //!< A room that finishes runs this long before it tells its players (0: at once), so that the last snapshots leave first
        double                  linger         = 0.3;                  //!< Then it waits this long for its last messages to leave

        std::uint64_t           mods           = 0;                    //!< Mods::fingerprint() of the server: a client whose mods are not these is refused (JoinError::Mods). 0: no mods.

        //! Who a player is. Called by the lobby for every request to join a room, with the token the client gave (MatchmakingClient::setToken(),
        //! empty if none) and the address it comes from (without the port), on the lobby's thread, so it must be quick (a check of a signature
        //! or a table; ask a backend ahead of time, not here). It answers the id of the player (not 0), which is Player::playerId in the room,
        //! or nothing: the player is refused with JoinError::Unauthenticated. The same id twice at once is refused as AlreadyJoined, so an
        //! account is in one room at a time. Empty (the default): nobody is checked, and the lobby gives its own ids.
        std::function<std::optional<std::uint32_t>(const std::string& token, const std::string& address)> authenticate;

        //! Who plays with whom. Called by the lobby for a JoinRoom (not for CreateRoom or JoinNamedRoom, which are the client's choice) with the
        //! request and the rooms of that kind that have a place (public, open, not ending), on the lobby's thread, so it must be quick. It answers
        //! a room to join, to make one (the lobby refuses if `maxRooms` are running), or to wait: the player stays in the queue and the picker
        //! is asked again at each tick, with a larger `waited`, until it answers something else or the player calls leave(). An answer that is not
        //! one of the rooms it was given is refused (UnknownRoom). Nothing (std::nullopt) means the usual rule: the fullest room that has a place,
        //! or a new one. Empty (the default): always the usual rule.
        std::function<std::optional<RoomChoice>(const JoinRequest&, const std::vector<net::RoomInfo>&)> roomPicker;

        double                  statsEvery     = 5.0;                  //!< A room logs its network numbers this often (INFO, category "server"; 0: never)

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
        std::string     roomName;            //!< What its creator called it ("<type> #<id>" for a room that the lobby made)
        bool            isPrivate = false;   //!< In no list: only its name and its id let anyone in
        std::string     options;             //!< What its creator set (net::CreateRoom::options): opaque to KUGE, empty for a room the lobby made
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
        std::atomic<std::size_t>  authRefused{0};    //!< Requests that ServerConfig::authenticate turned down (also counted in `refused`)
    };

    // -- What lobby and rooms say to each other (scene messages: no network) ----------------------
    namespace detail
    {
        struct ExpectPlayer  { std::uint32_t playerId; std::string name; std::uint64_t token; bool rejoin = false; };
        struct KickPlayer    { std::uint32_t playerId; net::RoomEnd reason; };
        struct RoomReady     { std::uint32_t roomId; };
        struct RoomOpen      { std::uint32_t roomId; bool open; };   //!< The room takes newcomers again, or no more (RoomScene::setOpen)
        struct RoomClosing   { std::uint32_t roomId; net::RoomEnd reason; };   //!< The game ended: no more players, and the ones it has may join again
        struct RoomFailed    { std::uint32_t roomId; std::string why; };
        struct PlayerJoined  { std::uint32_t roomId; std::uint32_t playerId; };
        struct PlayerLeft    { std::uint32_t roomId; std::uint32_t playerId; };
        struct RoomEnded     { std::uint32_t roomId; net::RoomEnd reason; };
    }

}
