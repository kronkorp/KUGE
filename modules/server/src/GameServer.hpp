#pragma once

#include "Engine.hpp"
#include "RoomScene.hpp"
#include "ServerConfig.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>

namespace kuge::server
{

    //! What the lobby needs to know, shared with it (built by GameServer, then only read)
    struct ServerState
    {
        using RoomFactory = std::function<std::unique_ptr<RoomScene>(RoomInit)>;

        struct RoomType
        {
            RoomTypeConfig config;
            RoomFactory    factory;
        };

        ServerConfig                      config;
        std::map<std::string, RoomType>   roomTypes;
        ServerStats                       stats;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A game server with no window: a lobby where clients arrive, and
     *         the rooms that it makes for them.
     *
     *     kuge::server::GameServer server({.lobbyPort = 4242});
     *     server.addRoomType<DeathmatchRoom>("deathmatch", {.maxPlayers = 8});
     *     return server.run();                  // until stop() or Ctrl+C
     *
     * The lobby is a scene of its own thread. A client asks it for a room of a kind
     * (see MatchmakingClient); it finds one with a free place, or makes one (a scene on its
     * own thread, or on the workers) up to ServerConfig::maxRooms, and gives the client
     * the address of the room with a token that opens its door once. The connection to
     * the lobby stays: its loss takes the player out of the room. When a room ends, the
     * lobby gets its slot and its port back, and the clients are back in the lobby.
     */
    ////////////////////////////////////////////////////////////////////////////
    class GameServer
    {
        public:
            explicit GameServer(ServerConfig config = {});
            ~GameServer(void);

            GameServer(const GameServer&)            = delete;
            GameServer& operator=(const GameServer&) = delete;

            //! A kind of room, made from a class derived from RoomScene (built with a RoomInit)
            template<typename Room>
            void addRoomType(const std::string& name, RoomTypeConfig config = {})
            {
                static_assert(std::is_base_of_v<RoomScene, Room>, "a room derives from kuge::server::RoomScene");
                addRoomType(name, config, [](RoomInit init) -> std::unique_ptr<RoomScene> { return std::make_unique<Room>(std::move(init)); });
            }

            void addRoomType(const std::string& name, RoomTypeConfig config, ServerState::RoomFactory factory);

            //! Opens the lobby, and runs until stop(), SIGINT or SIGTERM. All the rooms are closed before it returns.
            //! @return  0
            int run(void);

            //! Opens the lobby without running the loop (for whoever runs the engine: see engine())
            void start(void);

            //! Asks run() to end. Any thread.
            void stop(void) noexcept;

            Engine&             engine(void) noexcept { return m_engine; }
            const ServerStats&  stats(void) const noexcept { return m_state->stats; }
            const ServerConfig& config(void) const noexcept { return m_state->config; }

        private:
            std::shared_ptr<ServerState> m_state;
            Engine                       m_engine;
            bool                         m_started = false;
    };

}
