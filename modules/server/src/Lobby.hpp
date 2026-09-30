#pragma once

#include "GameServer.hpp"
#include "Net.hpp"
#include "Scene.hpp"
#include <map>
#include <set>

namespace kuge::server
{

    class LobbyHousekeeping;

    //! The scene where clients arrive (made by GameServer)
    class LobbyScene final : public Scene
    {
        public:
            explicit LobbyScene(std::shared_ptr<ServerState> state);

            void onEnter(void) override;
            void onExit(void) override;
            void onMessage(const Message& message) override;

        private:
            friend class LobbyHousekeeping;

            struct Member
            {
                std::uint32_t      playerId;
                net::ConnectionId  connection;   //!< In the lobby
                std::uint32_t      roomId;
                std::uint64_t      token;
                std::string        name;
                double             issuedAt;
                bool               joined = false;   //!< The room welcomed it
            };

            struct Room
            {
                std::uint32_t            id;
                std::string              type;
                std::string              name;
                bool                     isPrivate = false;   //!< Not in the lists, not filled by the automatic matchmaking
                SceneHandle              handle;
                std::uint16_t            port = 0;
                std::string              address;
                std::size_t              maxPlayers;
                bool                     ready = false;
                bool                     closing = false;   //!< The game ended: it takes no one more
                std::set<std::uint32_t>  members;      //!< Player ids: joined, or on their way
                std::vector<std::uint32_t> waiting;    //!< Told where the room is once it is ready
            };

            //! What a client asks for when it wants a room: the lobby chooses one (Auto), makes one that the
            //! client names (Create), or takes the one that the client names (Named)
            struct Want
            {
                enum class Kind : std::uint8_t { Auto, Create, Named };

                Kind          kind = Kind::Auto;
                std::string   type;            //!< Auto and Create
                std::uint32_t roomId = 0;      //!< Named
                std::string   roomName;        //!< Create: the name to give. Named: the name the room must have.
                std::string   playerName;
                bool          isPrivate = false;
                std::uint16_t protocol = net::MATCHMAKING_VERSION;
            };

            void request(net::ConnectionId from, Want want);
            void handleList(net::ConnectionId from, const net::ListRooms& request);
            void handleLeave(net::ConnectionId from);
            void handleGone(net::ConnectionId from);
            Room* findRoom(const std::string& type);
            Room* makeRoom(const std::string& type, const ServerState::RoomType& kind, const std::string& name, bool isPrivate);
            void enter(net::ConnectionId from, Room& room, const std::string& playerName);
            void assign(const Member& member, const Room& room);
            void removeMember(std::uint32_t playerId, bool tellRoom, net::RoomEnd reason);
            void endRoom(std::uint32_t roomId, net::RoomEnd reason, bool keepPort = false);
            void refuse(net::ConnectionId to, net::JoinError why);
            void housekeeping(void);
            bool tryJoin(net::ConnectionId from, const Want& want, bool final);
            void refreshStats(void);

            std::shared_ptr<ServerState>            m_state;
            net::Endpoint*                          m_endpoint = nullptr;
            std::map<std::uint32_t, Room>           m_rooms;
            std::map<std::uint32_t, Member>         m_members;       // by player id
            std::map<net::ConnectionId, std::uint32_t> m_byConnection;   // one room per client
            std::set<std::uint16_t>                 m_usedPorts;
            struct Retry
            {
                net::ConnectionId  connection;
                Want               want;
                double             until;
            };
            std::vector<Retry>                      m_retries;   //!< Joins that came before the lobby knew that the last room ended
            std::uint32_t                           m_nextRoom = 1;
            std::uint32_t                           m_nextPlayer = 1;
    };

}
