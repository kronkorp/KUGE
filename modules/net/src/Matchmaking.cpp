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
            }
            return "refused";
        }
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
            if (m_pending) {
                m_state = State::Joining;
                m_lobby->send(id, *m_pending);
                m_pending.reset();
            }
        });
        lobby.onDisconnected([this](ConnectionId, DisconnectReason) {
            m_lobby = nullptr;
            dropRoom();
            fail("the lobby was lost");
        });
        lobby.on<RoomAssigned>([this](ConnectionId, const RoomAssigned& assigned) { connectRoom(assigned); });
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

    void MatchmakingClient::join(const std::string& roomType, const std::string& playerName)
    {
        JoinRoom request;

        request.roomType = roomType;
        request.playerName = playerName;
        m_error.clear();
        if (m_lobby && m_lobby->connected() && (m_state == State::InLobby || m_state == State::Failed)) {
            m_state = State::Joining;
            m_lobby->send(m_lobby->connections().front(), request);
        } else {
            m_pending = request;   // sent when the lobby answers
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
