#include "Matchmaking.hpp"

namespace kuge::net
{

    namespace
    {
        const char* describe(JoinError error)
        {
            switch (error) {
                case JoinError::UnknownRoomType: return "the server has no such kind of room";
                case JoinError::Full:            return "every room is full";
                case JoinError::AlreadyJoined:   return "already in a room";
                case JoinError::RoomFailed:      return "the room could not start";
                case JoinError::Protocol:        return "another version of the protocol";
                case JoinError::InvalidName:     return "this is not a name for a room";
                case JoinError::UnknownRoom:     return "no such room";
            }
            return "refused";
        }

        // The length of the UTF-8 character that starts at text[i] (0: it is not one), as RFC 3629 has them:
        // no overlong form, no surrogate, nothing past U+10FFFF
        std::size_t characterLength(std::string_view text, std::size_t i)
        {
            const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
            const auto continuation = [&](std::size_t at) { return at < text.size() && (byte(at) & 0xC0) == 0x80; };
            const unsigned char first = byte(i);

            if (first < 0x80) {
                return 1;
            }
            if (first >= 0xC2 && first <= 0xDF) {
                return continuation(i + 1) ? 2 : 0;
            }
            if (first >= 0xE0 && first <= 0xEF) {
                if (!continuation(i + 1) || !continuation(i + 2)) {
                    return 0;
                }
                if (first == 0xE0 && byte(i + 1) < 0xA0) {
                    return 0;   // overlong
                }
                if (first == 0xED && byte(i + 1) >= 0xA0) {
                    return 0;   // a surrogate
                }
                return 3;
            }
            if (first >= 0xF0 && first <= 0xF4) {
                if (!continuation(i + 1) || !continuation(i + 2) || !continuation(i + 3)) {
                    return 0;
                }
                if (first == 0xF0 && byte(i + 1) < 0x90) {
                    return 0;   // overlong
                }
                if (first == 0xF4 && byte(i + 1) >= 0x90) {
                    return 0;   // past U+10FFFF
                }
                return 4;
            }
            return 0;
        }
    }

    std::string trimRoomName(std::string_view name)
    {
        const auto blank = [](char c) { return c == ' ' || c == '\t'; };

        while (!name.empty() && blank(name.front())) {
            name.remove_prefix(1);
        }
        while (!name.empty() && blank(name.back())) {
            name.remove_suffix(1);
        }
        return std::string(name);
    }

    bool validRoomName(std::string_view name)
    {
        if (name.empty() || name.size() > MAX_ROOM_NAME || name.front() == ' ' || name.front() == '\t' || name.back() == ' ' || name.back() == '\t') {
            return false;
        }
        for (std::size_t i = 0; i < name.size();) {
            const std::size_t length = characterLength(name, i);
            const auto first = static_cast<unsigned char>(name[i]);

            if (length == 0 || first < 0x20 || first == 0x7F) {
                return false;   // not UTF-8, or a control character
            }
            if (first == 0xC2 && static_cast<unsigned char>(name[i + 1]) < 0xA0) {
                return false;   // U+0080 to U+009F: the control characters of Latin-1
            }
            i += length;
        }
        return true;
    }

    MatchmakingClient::~MatchmakingClient(void)
    {
        // The endpoints are the Net's: their handlers must not call this object any more
        if (m_lobby) {
            m_lobby->onConnected({});
            m_lobby->onDisconnected({});
        }
        if (m_room) {
            m_room->onConnected({});
            m_room->onDisconnected({});
        }
    }

    void MatchmakingClient::fail(const std::string& why)
    {
        m_state = State::Failed;
        m_error = why;
        if (m_onFailed) {
            m_onFailed(why);
        }
    }

    void MatchmakingClient::bindLobby(Endpoint& lobby)
    {
        m_lobby = &lobby;
        m_state = State::ConnectingLobby;
        lobby.onConnected([this](ConnectionId id) {
            if (m_state == State::ConnectingLobby) {
                m_state = State::InLobby;
            }
            if (m_pendingList) {
                m_lobby->send(id, *m_pendingList);
                m_pendingList.reset();
            }
            if (m_pending) {
                m_state = State::Joining;
                std::visit([this, id](const auto& request) { m_lobby->send(id, request); }, *m_pending);
                m_pending.reset();
            }
        });
        lobby.onDisconnected([this](ConnectionId, DisconnectReason) {
            const bool inRoom = m_state == State::ConnectingRoom || m_state == State::InRoom;
            Endpoint* gone = m_lobby;

            m_lobby = nullptr;
            dropRoom();
            if (gone) {
                m_net.remove(*gone);   // (so that connectLobby() can be called again: to try once more)
            }
            if (inRoom && m_onRoomClosed) {
                m_onRoomClosed(RoomEnd::Error);   // the game holds the room's endpoint: it must hear that it is gone
            }
            fail("the lobby was lost");
        });
        lobby.on<RoomAssigned>([this](ConnectionId, const RoomAssigned& assigned) { connectRoom(assigned); });
        lobby.on<RoomList>([this](ConnectionId, const RoomList& list) {
            if (m_onRoomList) {
                m_onRoomList(list);
            }
        });
        lobby.on<JoinRefused>([this](ConnectionId, const JoinRefused& refused) {
            m_state = State::InLobby;
            m_error = describe(refused.reason);
            if (m_onFailed) {
                m_onFailed(m_error);
            }
        });
        lobby.on<RoomClosed>([this](ConnectionId, const RoomClosed& closed) {
            // The lobby says it when the room never saw us, or we were sent away
            if (m_state == State::ConnectingRoom || m_state == State::InRoom || m_state == State::Joining) {
                dropRoom();
                m_state = State::InLobby;
                if (m_onRoomClosed) {
                    m_onRoomClosed(closed.reason);
                }
            }
        });
    }

    void MatchmakingClient::connectLobby(Protocol protocol, const std::string& host, std::uint16_t port, EndpointConfig config)
    {
        m_sockets = true;
        m_host = host;
        m_config = config;
        bindLobby(m_net.connect(protocol, host, port, std::move(config)));
    }

    void MatchmakingClient::connectLobby(const std::string& name, LoopbackNetwork& network, EndpointConfig config)
    {
        m_sockets = false;
        m_network = &network;
        m_host = name;
        m_config = config;
        bindLobby(m_net.connect(name, network, std::move(config)));
    }

    // Asks the lobby for a room, now if it can be, when it answers if not
    void MatchmakingClient::start(Request request)
    {
        m_error.clear();
        if (m_lobby && m_lobby->connected() && (m_state == State::InLobby || m_state == State::Failed)) {
            m_state = State::Joining;
            std::visit([this](const auto& what) { m_lobby->send(m_lobby->connections().front(), what); }, request);
        } else {
            m_pending = std::move(request);   // sent when the lobby answers
        }
    }

    void MatchmakingClient::join(const std::string& roomType, const std::string& playerName)
    {
        JoinRoom request;

        request.roomType = roomType;
        request.playerName = playerName;
        start(std::move(request));
    }

    void MatchmakingClient::createRoom(const std::string& roomType, const std::string& roomName, const std::string& playerName, bool isPrivate)
    {
        CreateRoom request;

        request.roomType = roomType;
        request.roomName = roomName;
        request.playerName = playerName;
        request.isPrivate = isPrivate;
        start(std::move(request));
    }

    void MatchmakingClient::joinRoom(std::uint32_t roomId, const std::string& roomName, const std::string& playerName)
    {
        JoinNamedRoom request;

        request.roomId = roomId;
        request.roomName = roomName;
        request.playerName = playerName;
        start(std::move(request));
    }

    void MatchmakingClient::requestRooms(const std::string& roomType)
    {
        ListRooms request;

        request.roomType = roomType;
        if (m_lobby && m_lobby->connected()) {
            m_lobby->send(m_lobby->connections().front(), request);
        } else {
            m_pendingList = std::move(request);   // sent when the lobby answers
        }
    }

    void MatchmakingClient::connectRoom(const RoomAssigned& assigned)
    {
        if (m_state != State::Joining) {
            return;
        }
        m_state = State::ConnectingRoom;
        m_networkId = 0;
        const std::uint64_t token = assigned.token;

        if (m_sockets) {
            m_room = &m_net.connect(Protocol::Udp, assigned.address.empty() ? m_host : assigned.address, assigned.port, m_config);
        } else {
            m_room = &m_net.connect(assigned.address, *m_network, m_config);
        }
        Endpoint* room = m_room;

        room->onConnected([room, token](ConnectionId id) { room->send(id, Hello{token}); });
        room->on<Welcome>([this](ConnectionId, const Welcome& welcome) {
            m_state = State::InRoom;
            m_networkId = welcome.networkId;
            if (m_onJoined) {
                m_onJoined(*m_room, welcome);
            }
        });
        room->on<Rejected>([this](ConnectionId, const Rejected&) {
            dropRoom();
            m_state = State::InLobby;
            m_error = "the room did not accept the player";
            if (m_onFailed) {
                m_onFailed(m_error);
            }
        });
        room->on<RoomClosed>([this](ConnectionId, const RoomClosed& closed) {
            dropRoom();
            m_state = State::InLobby;
            if (m_onRoomClosed) {
                m_onRoomClosed(closed.reason);
            }
        });
        room->onDisconnected([this, room](ConnectionId, DisconnectReason) {
            if (m_room != room) {
                return;   // (already left)
            }
            m_room = nullptr;
            m_net.remove(*room);
            if (m_state == State::ConnectingRoom || m_state == State::InRoom) {
                m_state = State::InLobby;
                m_error = "the connection to the room was lost";
                if (m_onRoomClosed) {
                    m_onRoomClosed(RoomEnd::Error);
                }
            }
        });
    }

    void MatchmakingClient::dropRoom(void)
    {
        if (m_room) {
            Endpoint* room = m_room;

            m_room = nullptr;
            room->onDisconnected({});
            room->onConnected({});
            m_net.remove(*room);
        }
        m_networkId = 0;
    }

    void MatchmakingClient::leave(void)
    {
        m_pending.reset();
        m_pendingList.reset();
        if (m_state == State::Joining || m_state == State::ConnectingRoom || m_state == State::InRoom) {
            if (m_lobby && m_lobby->connected()) {
                m_lobby->send(m_lobby->connections().front(), LeaveRoom{});
            }
            dropRoom();
            m_state = State::InLobby;
        }
    }

    void MatchmakingClient::disconnect(void)
    {
        m_pending.reset();
        dropRoom();
        if (m_lobby) {
            Endpoint* lobby = m_lobby;

            m_lobby = nullptr;
            lobby->onDisconnected({});
            lobby->onConnected({});
            m_net.remove(*lobby);
        }
        m_state = State::Idle;
    }

}
