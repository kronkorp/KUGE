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
            Logger::logger().category("server").info("lobby: listening on port {}", config.lobbyPort);
        } else {
            net::LoopbackNetwork& loopback = config.loopback ? *config.loopback : net::LoopbackNetwork::global();

            m_endpoint = &network.listen(config.lobbyName, loopback, config.endpoint);
            Logger::logger().category("server").info("lobby: listening at '{}'", config.lobbyName);
        }
        m_state->stats.lobbyOpen = true;
        m_endpoint->onConnected([this](net::ConnectionId) { refreshStats(); });
        m_endpoint->onDisconnected([this](net::ConnectionId id, net::DisconnectReason reason) { handleGone(id, reason); });
        m_endpoint->on<net::RejoinRoom>([this](net::ConnectionId from, const net::RejoinRoom& rejoin) { handleRejoin(from, rejoin); });
        m_endpoint->on<net::JoinRoom>([this](net::ConnectionId from, const net::JoinRoom& join) {
            Want want;

            want.kind = Want::Kind::Auto;
            want.type = join.roomType;
            want.options = join.options;
            want.playerName = join.playerName;
            want.protocol = join.protocol;
            want.mods = join.mods;
            want.token = join.token;
            request(from, std::move(want));
        });
        m_endpoint->on<net::CreateRoom>([this](net::ConnectionId from, const net::CreateRoom& create) {
            Want want;

            want.kind = Want::Kind::Create;
            want.type = create.roomType;
            want.roomName = create.roomName;
            want.playerName = create.playerName;
            want.isPrivate = create.isPrivate;
            want.options = create.options;
            want.protocol = create.protocol;
            want.mods = create.mods;
            want.token = create.token;
            request(from, std::move(want));
        });
        m_endpoint->on<net::JoinNamedRoom>([this](net::ConnectionId from, const net::JoinNamedRoom& join) {
            Want want;

            want.kind = Want::Kind::Named;
            want.roomId = join.roomId;
            want.roomName = join.roomName;
            want.playerName = join.playerName;
            want.protocol = join.protocol;
            want.mods = join.mods;
            want.token = join.token;
            request(from, std::move(want));
        });
        m_endpoint->on<net::ListRooms>([this](net::ConnectionId from, const net::ListRooms& list) { handleList(from, list); });
        m_endpoint->on<net::LeaveRoom>([this](net::ConnectionId from, const net::LeaveRoom&) { handleLeave(from); });
        addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<LobbyHousekeeping>(*this));
    }

    void LobbyScene::onExit(void)
    {
        // The players still in a room are told why they are leaving, before their connection goes (the room tells
        // them too: whichever arrives first is heard, the other finds the client already in the lobby)
        if (m_endpoint) {
            for (const auto& [playerId, member] : m_members) {
                const auto room = m_rooms.find(member.roomId);
                const bool closing = room != m_rooms.end() && room->second.closing;

                m_endpoint->send(member.connection, net::RoomClosed{closing ? room->second.endReason : net::RoomEnd::ServerStopping});
            }
        }
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

    // A room that a request for "any room of this kind" may be put in. A private room is never filled by chance.
    bool LobbyScene::joinable(const Room& room, const std::string& type)
    {
        return room.type == type && !room.isPrivate && room.open && !room.closing && room.members.size() < room.maxPlayers;
    }

    net::RoomInfo LobbyScene::infoOf(const Room& room)
    {
        return net::RoomInfo{room.id, room.type, room.name, static_cast<std::uint32_t>(room.members.size()),
            static_cast<std::uint32_t>(room.maxPlayers), room.options, room.open};
    }

    LobbyScene::Room* LobbyScene::findRoom(const std::string& type)
    {
        Room* best = nullptr;

        for (auto& [id, room] : m_rooms) {
            // The fullest room that still has a place: games start sooner
            if (joinable(room, type) && (!best || room.members.size() > best->members.size())) {
                best = &room;
            }
        }
        return best;
    }

    // @param name  Empty: "<type> #<id>"
    LobbyScene::Room* LobbyScene::makeRoom(const std::string& type, const ServerState::RoomType& kind, const std::string& name, bool isPrivate, const std::string& options)
    {
        const ServerConfig& config = m_state->config;
        RoomInit init;

        init.roomId = m_nextRoom;
        init.roomType = type;
        init.roomName = name.empty() ? type + " #" + std::to_string(m_nextRoom) : name;
        init.isPrivate = isPrivate;
        init.options = options;
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
                Logger::logger().category("server").warn("lobby: no free port for a room of '{}'", type);
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
            room.name = init.roomName;
            room.isPrivate = isPrivate;
            room.options = options;
            room.port = init.port;
            room.address = init.address;
            room.maxPlayers = kind.config.maxPlayers;
            room.handle = ctx().engine().spawnScene(kind.config.policy, std::move(scene), ctx().self());
        } catch (const std::exception& error) {
            Logger::logger().category("server").error("lobby: cannot make a room of '{}': {}", type, error.what());
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
        assigned.playerId = member.playerId;
        assigned.rejoinKey = member.rejoinKey;
        m_endpoint->send(member.connection, assigned);
    }

    void LobbyScene::request(net::ConnectionId from, Want want)
    {
        want.since = m_state->config.now();
        if (const auto& authenticate = m_state->config.authenticate) {
            std::optional<std::uint32_t> who;

            if (want.token.size() <= net::MAX_AUTH_TOKEN) {
                who = authenticate(want.token, net::addressOf(m_endpoint->peer(from)));
            }
            if (!who || *who == 0) {
                ++m_state->stats.authRefused;
                refuse(from, net::JoinError::Unauthenticated);
                return;
            }
            const auto other = m_members.find(*who);

            if (other != m_members.end() && other->second.connection != from) {
                refuse(from, net::JoinError::AlreadyJoined);   // an account is in one room at a time (it comes back with RejoinRoom)
                return;
            }
            want.playerId = *who;
        }
        if (!tryJoin(from, want, false)) {
            m_retries.push_back(Retry{from, std::move(want), m_state->config.now() + 0.25});
        }
    }

    // The public rooms, by id. A private one is never told, nor is one that is ending.
    void LobbyScene::handleList(net::ConnectionId from, const net::ListRooms& list)
    {
        net::RoomList reply;

        for (const auto& [id, room] : m_rooms) {
            if (room.isPrivate || room.closing || (!list.roomType.empty() && room.type != list.roomType)) {
                continue;
            }
            ++reply.total;
            if (reply.rooms.size() < net::MAX_LISTED_ROOMS) {
                reply.rooms.push_back(infoOf(room));
            }
        }
        m_endpoint->send(from, reply);
    }

    // What a room does when a player is put in it: the room is told to expect the token, the client is told where to go
    void LobbyScene::enter(net::ConnectionId from, Room& room, const std::string& playerName, std::uint32_t playerId)
    {
        Member member{playerId ? playerId : m_nextPlayer++, from, room.id, newToken(), playerName, m_state->config.now(), false};

        member.rejoinKey = newToken();

        room.handle.send(detail::ExpectPlayer{member.playerId, member.name, member.token});
        room.members.insert(member.playerId);
        m_byConnection[from] = member.playerId;
        ++m_state->stats.joins;
        if (room.ready) {
            assign(member, room);
        } else {
            room.waiting.push_back(member.playerId);   // told when the room is listening
        }
        m_members[member.playerId] = std::move(member);
        refreshStats();
    }

    // @param final  false: a client that seems to be in a room already may be one whose room just ended,
    //               and the lobby has not heard yet: the join is tried again for a moment before it is refused
    bool LobbyScene::tryJoin(net::ConnectionId from, const Want& want, bool final)
    {
        if (want.protocol != net::MATCHMAKING_VERSION) {
            refuse(from, net::JoinError::Protocol);
            return true;
        }
        if (want.mods != m_state->config.mods) {
            refuse(from, net::JoinError::Mods);   // the server decides what is played
            return true;
        }
        const ServerState::RoomType* kind = nullptr;
        std::string name = net::trimRoomName(want.roomName);

        if (want.kind != Want::Kind::Named) {
            const auto found = m_state->roomTypes.find(want.type);

            if (found == m_state->roomTypes.end()) {
                refuse(from, net::JoinError::UnknownRoomType);
                return true;
            }
            kind = &found->second;
        }
        if (want.kind == Want::Kind::Create && !net::validRoomName(name)) {
            refuse(from, net::JoinError::InvalidName);
            return true;
        }
        if (want.kind == Want::Kind::Create && !net::validRoomOptions(want.options)) {
            refuse(from, net::JoinError::InvalidOptions);
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
        Room* room = nullptr;

        switch (want.kind) {
            case Want::Kind::Auto: {
                std::optional<RoomChoice> choice;

                if (!want.options.empty() && !net::validRoomOptions(want.options)) {
                    refuse(from, net::JoinError::InvalidOptions);
                    return true;
                }
                if (m_state->config.roomPicker) {
                    std::vector<net::RoomInfo> open;

                    for (const auto& [id, candidate] : m_rooms) {
                        if (joinable(candidate, want.type)) {
                            open.push_back(infoOf(candidate));
                        }
                    }
                    choice = m_state->config.roomPicker(JoinRequest{want.type, want.playerName, want.options, want.playerId,
                        net::addressOf(m_endpoint->peer(from)), m_state->config.now() - want.since}, open);
                }
                if (choice && choice->kind == RoomChoice::Kind::Wait) {
                    return false;   // asked again at the next tick
                }
                if (choice && choice->kind == RoomChoice::Kind::Join) {
                    const auto chosen = m_rooms.find(choice->roomId);

                    if (chosen == m_rooms.end() || !joinable(chosen->second, want.type)) {
                        refuse(from, net::JoinError::UnknownRoom);
                        return true;
                    }
                    room = &chosen->second;
                    break;
                }
                if (choice && !net::validRoomOptions(choice->options)) {
                    refuse(from, net::JoinError::InvalidOptions);
                    return true;
                }
                if (!choice) {
                    room = findRoom(want.type);
                }
                if (!room && m_rooms.size() < m_state->config.maxRooms) {
                    room = makeRoom(want.type, *kind, "", false, choice ? choice->options : "");
                    if (!room) {
                        refuse(from, net::JoinError::RoomFailed);
                        return true;
                    }
                }
                break;
            }
            case Want::Kind::Create:
                if (m_rooms.size() >= m_state->config.maxRooms) {
                    refuse(from, net::JoinError::Full);
                    return true;
                }
                room = makeRoom(want.type, *kind, name, want.isPrivate, want.options);
                if (!room) {
                    refuse(from, net::JoinError::RoomFailed);
                    return true;
                }
                break;
            case Want::Kind::Named: {
                // The id and the name must both be the room's. One answer for every way to be wrong (no such room, another
                // name, a room that is ending), so that a private room cannot be told from one that does not exist.
                const auto found = m_rooms.find(want.roomId);

                if (found == m_rooms.end() || found->second.closing || found->second.name != name) {
                    refuse(from, net::JoinError::UnknownRoom);
                    return true;
                }
                room = &found->second;
                if (!room->open) {
                    refuse(from, net::JoinError::Started);
                    return true;
                }
                if (room->members.size() >= room->maxPlayers) {
                    refuse(from, net::JoinError::Full);
                    return true;
                }
                break;
            }
        }
        if (!room) {
            refuse(from, net::JoinError::Full);
            return true;
        }
        enter(from, *room, want.playerName, want.playerId);
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
        std::erase_if(m_retries, [from](const Retry& retry) { return retry.connection == from; });   // (a player who waits for a room gives up)
        const auto found = m_byConnection.find(from);

        if (found != m_byConnection.end()) {
            removeMember(found->second, true, net::RoomEnd::Kicked);
        }
    }

    // How long a room of this type keeps the place of a player whose connection is lost
    static double keepsPlaceFor(const ServerState& state, const std::string& roomType)
    {
        const auto kind = state.roomTypes.find(roomType);

        return kind == state.roomTypes.end() ? 0.0 : kind->second.config.reconnectSeconds;
    }

    void LobbyScene::handleGone(net::ConnectionId from, net::DisconnectReason reason)
    {
        const auto found = m_byConnection.find(from);

        // A lost connection (not one the client ended) to a room that keeps places: the player is away, not gone. The room finds out
        // by itself, and tells when its time is up.
        if (found != m_byConnection.end() && (reason == net::DisconnectReason::Timeout || reason == net::DisconnectReason::Unreachable)) {
            const auto member = m_members.find(found->second);
            const auto room = member == m_members.end() ? m_rooms.end() : m_rooms.find(member->second.roomId);

            if (room != m_rooms.end() && !room->second.closing && member->second.joined && keepsPlaceFor(*m_state, room->second.type) > 0.0) {
                member->second.away = true;
                member->second.awaySince = m_state->config.now();
                m_byConnection.erase(found);
                refreshStats();
                return;
            }
        }
        // The client left the lobby: it leaves its room too
        handleLeave(from);
        refreshStats();
    }

    // A player comes back from a new connection to the lobby (or the old one, if only the room was lost)
    void LobbyScene::handleRejoin(net::ConnectionId from, const net::RejoinRoom& rejoin)
    {
        if (rejoin.protocol != net::MATCHMAKING_VERSION) {
            refuse(from, net::JoinError::Protocol);
            return;
        }
        const auto found = m_members.find(rejoin.playerId);
        const auto room = found == m_members.end() ? m_rooms.end() : m_rooms.find(found->second.roomId);

        // One answer for every way to be wrong, so that player ids cannot be tried
        if (found == m_members.end() || room == m_rooms.end() || found->second.rejoinKey != rejoin.rejoinKey || found->second.roomId != rejoin.roomId
            || room->second.closing || !room->second.ready || keepsPlaceFor(*m_state, room->second.type) <= 0.0) {
            refuse(from, net::JoinError::UnknownRoom);
            return;
        }
        const auto other = m_byConnection.find(from);

        if (other != m_byConnection.end() && other->second != rejoin.playerId) {
            refuse(from, net::JoinError::AlreadyJoined);
            return;
        }
        Member& member = found->second;
        const auto old = m_byConnection.find(member.connection);

        if (old != m_byConnection.end() && old->second == rejoin.playerId) {
            m_byConnection.erase(old);
        }
        member.connection = from;
        member.away = false;
        member.joined = false;   // until the room welcomes it again
        member.token = newToken();
        member.issuedAt = m_state->config.now();
        m_byConnection[from] = rejoin.playerId;
        room->second.handle.send(detail::ExpectPlayer{member.playerId, member.name, member.token, true});
        assign(member, room->second);
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
        } else if (const auto* opening = message.as<detail::RoomOpen>()) {
            const auto room = m_rooms.find(opening->roomId);

            if (room != m_rooms.end()) {
                room->second.open = opening->open;
            }
        } else if (const auto* closing = message.as<detail::RoomClosing>()) {
            const auto room = m_rooms.find(closing->roomId);

            if (room != m_rooms.end()) {
                room->second.closing = true;
                room->second.endReason = closing->reason;
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
            if (!tryJoin(retry.connection, retry.want, time >= retry.until)) {
                m_retries.push_back(retry);
            }
        }
        for (const auto& [playerId, member] : m_members) {
            if (!member.joined && time - member.issuedAt > m_state->config.tokenTtl + 1.0) {
                stale.push_back(playerId);
            }
        }
        // A player away for longer than its room keeps the place (the room lets it go itself, at about the same time)
        for (const auto& [playerId, member] : m_members) {
            const auto room = m_rooms.find(member.roomId);

            if (member.away && room != m_rooms.end() && time - member.awaySince > keepsPlaceFor(*m_state, room->second.type) + 1.0) {
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
