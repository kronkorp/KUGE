#include "Lobby.hpp"
#include "Logger.hpp"
#include "Stage.hpp"
#include <algorithm>
#include <random>

namespace kuge::server
{

    namespace
    {
        std::uint64_t newToken(void)
        {
            static thread_local std::random_device device;

            return (static_cast<std::uint64_t>(device()) << 32) | device();
        }
    }

    class LobbyHousekeeping final : public kw::ISystem
    {
        public:
            explicit LobbyHousekeeping(LobbyScene& lobby) : m_lobby(lobby) {}

            bool handle(kw::World&) override
            {
                m_lobby.housekeeping();
                return true;
            }

        private:
            LobbyScene& m_lobby;
    };

    LobbyScene::LobbyScene(std::shared_ptr<ServerState> state) : m_state(std::move(state))
    {
    }

    void LobbyScene::onEnter(void)
    {
        const ServerConfig& config = m_state->config;

        net::installNet(setup());
        auto& network = world().getResource<net::Net>();

        if (config.transport == Transport::Sockets) {
            m_endpoint = &network.listen(config.lobbyProtocol, config.lobbyPort, config.endpoint);
            Logger::logger().info("lobby: listening on port {}", config.lobbyPort);
        } else {
            net::LoopbackNetwork& loopback = config.loopback ? *config.loopback : net::LoopbackNetwork::global();

            m_endpoint = &network.listen(config.lobbyName, loopback, config.endpoint);
            Logger::logger().info("lobby: listening at '{}'", config.lobbyName);
        }
        m_state->stats.lobbyOpen = true;
        m_endpoint->onConnected([this](net::ConnectionId) { refreshStats(); });
        m_endpoint->onDisconnected([this](net::ConnectionId id, net::DisconnectReason) { handleGone(id); });
        m_endpoint->on<net::JoinRoom>([this](net::ConnectionId from, const net::JoinRoom& request) { handleJoin(from, request); });
        m_endpoint->on<net::LeaveRoom>([this](net::ConnectionId from, const net::LeaveRoom&) { handleLeave(from); });
        addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<LobbyHousekeeping>(*this));
    }

    void LobbyScene::onExit(void)
    {
        // The rooms are the server's: they end with it (the engine stops every spawned scene)
        for (auto& [id, room] : m_rooms) {
            room.handle.stop();
        }
        m_state->stats.lobbyOpen = false;
        m_state->stats.rooms = 0;
        m_state->stats.players = 0;
        m_state->stats.lobbyClients = 0;
    }

    void LobbyScene::refreshStats(void)
    {
        m_state->stats.rooms = m_rooms.size();
        m_state->stats.players = m_members.size();
        m_state->stats.lobbyClients = m_endpoint ? m_endpoint->connections().size() : 0;
    }

    // -- Joining -----------------------------------------------------------------------------------
    void LobbyScene::refuse(net::ConnectionId to, net::JoinError why)
    {
        ++m_state->stats.refused;
        m_endpoint->send(to, net::JoinRefused{why});
    }

    LobbyScene::Room* LobbyScene::findRoom(const std::string& type)
    {
        Room* best = nullptr;

        for (auto& [id, room] : m_rooms) {
            // The fullest room that still has a place: games start sooner
            if (room.type == type && !room.closing && room.members.size() < room.maxPlayers && (!best || room.members.size() > best->members.size())) {
                best = &room;
            }
        }
        return best;
    }

    LobbyScene::Room* LobbyScene::makeRoom(const std::string& type, const ServerState::RoomType& kind)
    {
        const ServerConfig& config = m_state->config;
        RoomInit init;

        init.roomId = m_nextRoom;
        init.roomType = type;
        init.type = kind.config;
        init.server = config;
        if (config.transport == Transport::Sockets) {
            for (std::uint32_t i = 0; i < config.roomPortCount; ++i) {
                const auto port = static_cast<std::uint16_t>(config.roomPortFirst + i);

                if (!m_usedPorts.count(port)) {
                    init.port = port;
                    break;
                }
            }
            if (init.port == 0) {
                Logger::logger().warn("lobby: no free port for a room of '{}'", type);
                return nullptr;
            }
            m_usedPorts.insert(init.port);
        } else {
            init.address = "room-" + std::to_string(init.roomId);
        }
        Room room;

        try {
            auto scene = kind.factory(init);

            room.id = m_nextRoom++;
            room.type = type;
            room.port = init.port;
            room.address = init.address;
            room.maxPlayers = kind.config.maxPlayers;
            room.handle = ctx().engine().spawnScene(kind.config.policy, std::move(scene), ctx().self());
        } catch (const std::exception& error) {
            Logger::logger().error("lobby: cannot make a room of '{}': {}", type, error.what());
            m_usedPorts.erase(init.port);
            return nullptr;
        }
        ++m_state->stats.roomsMade;
        const auto id = room.id;

        m_rooms[id] = std::move(room);
        refreshStats();
        return &m_rooms[id];
    }

    void LobbyScene::assign(const Member& member, const Room& room)
    {
        net::RoomAssigned assigned;

        assigned.roomId = room.id;
        assigned.address = m_state->config.transport == Transport::Sockets ? m_state->config.roomAddress : room.address;
        assigned.port = room.port;
        assigned.token = member.token;
        assigned.tickRate = m_state->config.tickRate;
        m_endpoint->send(member.connection, assigned);
    }

    void LobbyScene::handleJoin(net::ConnectionId from, const net::JoinRoom& request)
    {
        if (!tryJoin(from, request, false)) {
            m_retries.push_back(Retry{from, request, m_state->config.now() + 0.25});
        }
    }

    // @param final  false: a client that seems to be in a room already may be one whose room just ended,
    //               and the lobby has not heard yet: the join is tried again for a moment before it is refused
    bool LobbyScene::tryJoin(net::ConnectionId from, const net::JoinRoom& request, bool final)
    {
        if (request.protocol != net::MATCHMAKING_VERSION) {
            refuse(from, net::JoinError::Protocol);
            return true;
        }
        const auto kind = m_state->roomTypes.find(request.roomType);

        if (kind == m_state->roomTypes.end()) {
            refuse(from, net::JoinError::UnknownRoomType);
            return true;
        }
        const auto already = m_byConnection.find(from);

        if (already != m_byConnection.end()) {
            const auto member = m_members.find(already->second);
            const auto room = member == m_members.end() ? m_rooms.end() : m_rooms.find(member->second.roomId);

            if (room != m_rooms.end() && room->second.closing) {
                removeMember(already->second, false, net::RoomEnd::GameOver);   // its game is over: it is free
            } else if (!final) {
                return false;   // the caller tries again in a moment
            } else {
                refuse(from, net::JoinError::AlreadyJoined);
                return true;
            }
        }
        Room* room = findRoom(request.roomType);

        if (!room && m_rooms.size() < m_state->config.maxRooms) {
            room = makeRoom(request.roomType, kind->second);
            if (!room) {
                refuse(from, net::JoinError::RoomFailed);
                return true;
            }
        }
        if (!room) {
            refuse(from, net::JoinError::Full);
            return true;
        }
        Member member{m_nextPlayer++, from, room->id, newToken(), request.playerName, m_state->config.now(), false};

        room->handle.send(detail::ExpectPlayer{member.playerId, member.name, member.token});
        room->members.insert(member.playerId);
        m_byConnection[from] = member.playerId;
        ++m_state->stats.joins;
        if (room->ready) {
            assign(member, *room);
        } else {
            room->waiting.push_back(member.playerId);   // told when the room is listening
        }
        m_members[member.playerId] = std::move(member);
        refreshStats();
        return true;
    }

    // -- Leaving -----------------------------------------------------------------------------------
    void LobbyScene::removeMember(std::uint32_t playerId, bool tellRoom, net::RoomEnd reason)
    {
        const auto found = m_members.find(playerId);

        if (found == m_members.end()) {
            return;
        }
        const Member member = found->second;
        const auto room = m_rooms.find(member.roomId);

        if (room != m_rooms.end()) {
            room->second.members.erase(playerId);
            std::erase(room->second.waiting, playerId);
            if (tellRoom) {
                room->second.handle.send(detail::KickPlayer{playerId, reason});
            }
        }
        m_byConnection.erase(member.connection);
        m_members.erase(found);
        refreshStats();
    }

    void LobbyScene::handleLeave(net::ConnectionId from)
    {
        const auto found = m_byConnection.find(from);

        if (found != m_byConnection.end()) {
            removeMember(found->second, true, net::RoomEnd::Kicked);
        }
    }

    void LobbyScene::handleGone(net::ConnectionId from)
    {
        // The client left the lobby: it leaves its room too
        handleLeave(from);
        refreshStats();
    }

    void LobbyScene::endRoom(std::uint32_t roomId, net::RoomEnd reason, bool keepPort)
    {
        const auto found = m_rooms.find(roomId);

        if (found == m_rooms.end()) {
            return;
        }
        // Whoever was in it is back in the lobby (the room told them itself, unless it died)
        const std::set<std::uint32_t> members = found->second.members;

        for (const auto playerId : members) {
            const auto member = m_members.find(playerId);

            if (member != m_members.end()) {
                m_endpoint->send(member->second.connection, net::RoomClosed{reason});
                m_byConnection.erase(member->second.connection);
                m_members.erase(member);
            }
        }
        if (!keepPort) {
            m_usedPorts.erase(found->second.port);   // (a port that could not be used stays taken: the next room tries another)
        }
        m_rooms.erase(found);
        refreshStats();
    }

    void LobbyScene::onMessage(const Message& message)
    {
        if (const auto* ready = message.as<detail::RoomReady>()) {
            const auto room = m_rooms.find(ready->roomId);

            if (room != m_rooms.end()) {
                room->second.ready = true;
                for (const auto playerId : room->second.waiting) {
                    const auto member = m_members.find(playerId);

                    if (member != m_members.end()) {
                        assign(member->second, room->second);
                    }
                }
                room->second.waiting.clear();
            }
        } else if (const auto* failed = message.as<detail::RoomFailed>()) {
            const auto room = m_rooms.find(failed->roomId);

            if (room != m_rooms.end()) {
                for (const auto playerId : std::set<std::uint32_t>(room->second.members)) {
                    const auto member = m_members.find(playerId);

                    if (member != m_members.end()) {
                        refuse(member->second.connection, net::JoinError::RoomFailed);
                        m_byConnection.erase(member->second.connection);
                        m_members.erase(member);
                    }
                }
                room->second.members.clear();
            }
            endRoom(failed->roomId, net::RoomEnd::Error, true);
        } else if (const auto* joined = message.as<detail::PlayerJoined>()) {
            const auto member = m_members.find(joined->playerId);

            if (member != m_members.end()) {
                member->second.joined = true;
            }
        } else if (const auto* left = message.as<detail::PlayerLeft>()) {
            // The player left the room (not the lobby): it can join again
            removeMember(left->playerId, false, net::RoomEnd::Kicked);
        } else if (const auto* closing = message.as<detail::RoomClosing>()) {
            const auto room = m_rooms.find(closing->roomId);

            if (room != m_rooms.end()) {
                room->second.closing = true;
            }
        } else if (const auto* ended = message.as<detail::RoomEnded>()) {
            endRoom(ended->roomId, ended->reason);
        }
    }

    // What the lobby does with the time: forgets the clients that never reached their room,
    // and the rooms that died without a word
    void LobbyScene::housekeeping(void)
    {
        const double time = m_state->config.now();
        std::vector<std::uint32_t> stale;
        std::vector<std::uint32_t> dead;

        // A join is tried again at each tick until the lobby has heard of the end of the last room, or the time is up
        std::vector<Retry> retries;

        retries.swap(m_retries);
        const std::vector<net::ConnectionId> clients = m_endpoint->connections();

        for (auto& retry : retries) {
            if (std::find(clients.begin(), clients.end(), retry.connection) == clients.end()) {
                continue;   // the client is gone
            }
            if (!tryJoin(retry.connection, retry.request, time >= retry.until)) {
                m_retries.push_back(retry);
            }
        }
        for (const auto& [playerId, member] : m_members) {
            if (!member.joined && time - member.issuedAt > m_state->config.tokenTtl + 1.0) {
                stale.push_back(playerId);
            }
        }
        for (const auto playerId : stale) {
            const auto member = m_members.find(playerId);
            const net::ConnectionId connection = member->second.connection;

            removeMember(playerId, true, net::RoomEnd::Kicked);
            m_endpoint->send(connection, net::RoomClosed{net::RoomEnd::Kicked});
        }
        for (const auto& [id, room] : m_rooms) {
            if (!room.handle.alive()) {
                dead.push_back(id);
            }
        }
        for (const auto id : dead) {
            endRoom(id, net::RoomEnd::Error);
        }
    }

}
