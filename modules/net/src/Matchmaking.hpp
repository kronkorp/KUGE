#pragma once

#include "Net.hpp"
#include <cstdint>
#include <functional>
#include <string>

namespace kuge::net
{

    // -- What client and server say to each other to get a player into a room ----------------------
    //
    //   client                       lobby (well known address)               room (own address)
    //     | -- JoinRoom ------------->  |                                          |
    //     | <-- RoomAssigned(address, token)                                       |
    //     | ---------------------------- connect ------------------------------> |
    //     | ---------------------------- Hello(token) -------------------------> |
    //     | <--------------------------- Welcome(networkId) --------------------- |
    //     |                       ... the game ...                                 |
    //     | <--------------------------- RoomClosed --------------------------- |
    //     | (back to the lobby, which the client never left)                       |

    //! What JoinRoom says of the rules of matchmaking (not of the network: see EndpointConfig::protocol)
    constexpr std::uint16_t MATCHMAKING_VERSION = 1;

    enum class JoinError : std::uint8_t {
        UnknownRoomType,   //!< The server has no such kind of room
        Full,              //!< Every room of this kind is full, and no more can be made
        AlreadyJoined,     //!< This client is in a room already
        RoomFailed,        //!< The room could not start
        Protocol,          //!< Another version of the protocol
    };

    enum class HelloError : std::uint8_t {
        UnknownToken,      //!< Not a token this room gave out (or one that was used, or that expired)
        Closing,           //!< The room is ending
    };

    enum class RoomEnd : std::uint8_t {
        GameOver,          //!< The game ended
        Idle,              //!< Nobody came, or everybody left
        ServerStopping,
        Kicked,            //!< Sent away (or never showed up)
        Error,
    };

    struct JoinRoom
    {
        KUGE_MESSAGE(JoinRoom, roomType, playerName, protocol)
        std::string   roomType;
        std::string   playerName;
        std::uint16_t protocol = MATCHMAKING_VERSION;
    };

    //! Where the room is, and the key to enter it. An empty address means "the machine of the lobby".
    struct RoomAssigned
    {
        KUGE_MESSAGE(RoomAssigned, roomId, address, port, token, tickRate)
        std::uint32_t roomId = 0;
        std::string   address;
        std::uint16_t port = 0;
        std::uint64_t token = 0;
        std::uint32_t tickRate = 60;
    };

    struct JoinRefused
    {
        KUGE_MESSAGE(JoinRefused, reason)
        JoinError reason = JoinError::Full;
    };

    //! The client leaves its room (or gives up joining one)
    struct LeaveRoom { KUGE_MESSAGE(LeaveRoom) };

    struct Hello
    {
        KUGE_MESSAGE(Hello, token)
        std::uint64_t token = 0;
    };

    //! The room accepted the client: NetworkId is how the game refers to this player in the room
    struct Welcome
    {
        KUGE_MESSAGE(Welcome, networkId, roomId, tickRate)
        std::uint32_t networkId = 0;
        std::uint32_t roomId = 0;
        std::uint32_t tickRate = 60;
    };

    struct Rejected
    {
        KUGE_MESSAGE(Rejected, reason)
        HelloError reason = HelloError::UnknownToken;
    };

    struct RoomClosed
    {
        KUGE_MESSAGE(RoomClosed, reason)
        RoomEnd reason = RoomEnd::GameOver;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The client side of joining a room: connects to a lobby, asks for a
     *         room, connects to it, says hello, and hands over the endpoint of
     *         the room once the server has welcomed the player.
     *
     *     kuge::net::MatchmakingClient matchmaking(net);
     *     matchmaking.connectLobby(kuge::net::Protocol::Tcp, "example.org", 4242);
     *     matchmaking.onJoined([](kuge::net::Endpoint& room, const kuge::net::Welcome& welcome) {
     *         room.on<GameState>(...);            // the game talks to the room through this
     *     });
     *     matchmaking.join("deathmatch", "Ana");  // may be called at once: it waits for the lobby
     *
     * It lives in the scene that owns the Net (the callbacks run in its poll). The
     * connection to the lobby stays after joining: when the room closes, the client
     * is back in the lobby and can join another. It must not be moved or copied, and
     * must outlive the endpoints of its Net (make it a member of the scene).
     */
    ////////////////////////////////////////////////////////////////////////////
    class MatchmakingClient
    {
        public:
            enum class State : std::uint8_t {
                Idle,             //!< Not connected to a lobby
                ConnectingLobby,
                InLobby,          //!< Connected to the lobby, in no room
                Joining,          //!< Asked for a room
                ConnectingRoom,   //!< Told where it is, saying hello
                InRoom,           //!< Welcomed
                Failed,           //!< Could not join, or the lobby was lost (see error())
            };

            explicit MatchmakingClient(Net& net) noexcept : m_net(net) {}
            ~MatchmakingClient(void);

            MatchmakingClient(const MatchmakingClient&)            = delete;
            MatchmakingClient& operator=(const MatchmakingClient&) = delete;

            //! The lobby of a server (Tcp or Udp)
            void connectLobby(Protocol protocol, const std::string& host, std::uint16_t port, EndpointConfig config = {});

            //! The lobby of a server of the same process, by name
            void connectLobby(const std::string& name, LoopbackNetwork& network = LoopbackNetwork::global(), EndpointConfig config = {});

            //! Asks for a room of a kind. Called before the lobby answered, it is sent when it does.
            void join(const std::string& roomType, const std::string& playerName);

            //! Leaves the room (or the wait for one). The lobby is kept.
            void leave(void);

            //! Says goodbye to the lobby and to the room
            void disconnect(void);

            void onJoined(std::function<void(Endpoint&, const Welcome&)> handler) { m_onJoined = std::move(handler); }
            //! The room is over, or lost: the endpoint that onJoined gave is gone (drop whatever holds it). Called
            //! for every way a room ends but leave() and disconnect(), which the caller asked for.
            void onRoomClosed(std::function<void(RoomEnd)> handler) { m_onRoomClosed = std::move(handler); }
            //! Joining did not work, or the lobby is lost
            void onFailed(std::function<void(const std::string&)> handler) { m_onFailed = std::move(handler); }

            State                state(void) const noexcept { return m_state; }
            const std::string&   error(void) const noexcept { return m_error; }
            Endpoint*            room(void) noexcept { return m_room; }
            Endpoint*            lobby(void) noexcept { return m_lobby; }
            std::uint32_t        networkId(void) const noexcept { return m_networkId; }

        private:
            void bindLobby(Endpoint& lobby);
            void connectRoom(const RoomAssigned& assigned);
            void dropRoom(void);
            void fail(const std::string& why);

            Net&                    m_net;
            Endpoint*               m_lobby = nullptr;
            Endpoint*               m_room = nullptr;
            State                   m_state = State::Idle;
            std::string             m_error;
            std::uint32_t           m_networkId = 0;

            // How the lobby was reached, to reach the rooms the same way
            bool                    m_sockets = true;
            std::string             m_host;
            LoopbackNetwork*        m_network = nullptr;
            EndpointConfig          m_config;

            std::optional<JoinRoom> m_pending;   // waiting for the lobby
            bool                    m_leaving = false;
            std::function<void(Endpoint&, const Welcome&)> m_onJoined;
            std::function<void(RoomEnd)>                   m_onRoomClosed;
            std::function<void(const std::string&)>        m_onFailed;
    };

}
