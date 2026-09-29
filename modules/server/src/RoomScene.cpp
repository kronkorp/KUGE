#include "RoomScene.hpp"
#include "Logger.hpp"
#include "SceneManager.hpp"
#include "Stage.hpp"
#include <algorithm>

namespace kuge::server
{

    namespace
    {
        constexpr double HELLO_GRACE = 1.0;   //!< A hello whose token the room does not know yet waits this long: the lobby's message may be on its way
    }

    class RoomHousekeeping final : public kw::ISystem
    {
        public:
            explicit RoomHousekeeping(RoomScene& room) : m_room(room) {}

            bool handle(kw::World&) override
            {
                m_room.housekeeping();
                return true;
            }

        private:
            RoomScene& m_room;
    };

    RoomScene::RoomScene(RoomInit init) : m_init(std::move(init))
    {
    }

    RoomScene::~RoomScene(void) = default;

    // -- Life --------------------------------------------------------------------------------------
    void RoomScene::onEnter(void)
    {
        net::EndpointConfig config = m_init.server.endpoint;

        config.maxConnections = m_init.type.maxPlayers * 2 + 4;   // (players, and the ones that have not said hello yet)
        net::installNet(setup());
        auto& network = world().getResource<net::Net>();

        try {
            if (m_init.server.transport == Transport::Sockets) {
                m_endpoint = &network.listen(net::Protocol::Udp, m_init.port, config);
            } else {
                net::LoopbackNetwork& loopback = m_init.server.loopback ? *m_init.server.loopback : net::LoopbackNetwork::global();

                m_endpoint = &network.listen(m_init.address, loopback, config);
            }
        } catch (const std::exception& error) {
            Logger::logger().error("room {}: cannot listen: {}", m_init.roomId, error.what());
            tellLobby(detail::RoomFailed{m_init.roomId, error.what()});
            m_ended = true;
            ctx().scenes().pop();
            return;
        }
        m_lastActive = now();
        m_endpoint->onConnected([this](net::ConnectionId id) { m_unknown[id] = now(); });
        m_endpoint->onDisconnected([this](net::ConnectionId id, net::DisconnectReason reason) { playerGone(id, reason); });
        m_endpoint->on<net::Hello>([this](net::ConnectionId from, const net::Hello& hello) { handleHello(from, hello.token); });
        addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<RoomHousekeeping>(*this));
        onRoomEnter();
        tellLobby(detail::RoomReady{m_init.roomId});
        Logger::logger().info("room {} ({}) is open", m_init.roomId, m_init.roomType);
    }

    void RoomScene::onExit(void)
    {
        onRoomExit();
        if (!m_ended) {
            m_ended = true;
            tellLobby(detail::RoomEnded{m_init.roomId, m_finishing ? m_endReason : net::RoomEnd::ServerStopping});
        }
        Logger::logger().info("room {} is closed", m_init.roomId);
    }

    void RoomScene::onMessage(const Message& message)
    {
        if (const auto* expect = message.as<detail::ExpectPlayer>()) {
            m_expected[expect->token] = Expected{expect->name, expect->playerId, now() + m_init.server.tokenTtl};
        } else if (const auto* kickOne = message.as<detail::KickPlayer>()) {
            for (auto it = m_expected.begin(); it != m_expected.end();) {
                it = it->second.playerId == kickOne->playerId ? m_expected.erase(it) : std::next(it);
            }
            for (const auto& [networkId, who] : m_players) {
                if (who.playerId == kickOne->playerId) {
                    kick(networkId, kickOne->reason);
                    break;
                }
            }
        } else {
            onRoomMessage(message);
        }
    }

    // -- Players -----------------------------------------------------------------------------------
    const RoomScene::Player* RoomScene::player(net::ConnectionId connection) const
    {
        for (const auto& [id, who] : m_players) {
            if (who.connection == connection) {
                return &who;
            }
        }
        return nullptr;
    }

    const RoomScene::Player* RoomScene::playerByNetworkId(std::uint32_t networkId) const
    {
        const auto found = m_players.find(networkId);

        return found == m_players.end() ? nullptr : &found->second;
    }

    void RoomScene::reject(net::ConnectionId from, net::HelloError why)
    {
        m_endpoint->send(from, net::Rejected{why});
        m_unknown.erase(from);
        m_endpoint->disconnect(from);
    }

    bool RoomScene::admit(net::ConnectionId from, std::uint64_t token)
    {
        const auto found = m_expected.find(token);

        if (found == m_expected.end()) {
            return false;
        }
        Player who{found->second.playerId, found->second.name, m_nextNetworkId++, from};

        m_expected.erase(found);        // a token opens the door once
        m_unknown.erase(from);
        m_players[who.networkId] = who;
        m_endpoint->send(from, net::Welcome{who.networkId, m_init.roomId, m_init.server.tickRate});
        tellLobby(detail::PlayerJoined{m_init.roomId, who.playerId});
        onPlayerJoined(who);
        return true;
    }

    void RoomScene::handleHello(net::ConnectionId from, std::uint64_t token)
    {
        if (player(from)) {
            return;   // already in
        }
        if (m_finishing) {
            reject(from, net::HelloError::Closing);
        } else if (!admit(from, token)) {
            m_waiting.push_back(WaitingHello{from, token, now()});
        }
    }

    void RoomScene::playerGone(net::ConnectionId connection, net::DisconnectReason reason)
    {
        m_unknown.erase(connection);
        std::erase_if(m_waiting, [connection](const WaitingHello& hello) { return hello.connection == connection; });
        for (auto it = m_players.begin(); it != m_players.end(); ++it) {
            if (it->second.connection == connection) {
                const Player who = it->second;

                m_players.erase(it);
                m_lastActive = now();
                tellLobby(detail::PlayerLeft{m_init.roomId, who.playerId});
                onPlayerLeft(who, reason);
                return;
            }
        }
    }

    void RoomScene::kick(std::uint32_t networkId, net::RoomEnd reason)
    {
        const Player* who = playerByNetworkId(networkId);

        if (who) {
            const net::ConnectionId connection = who->connection;

            m_endpoint->send(connection, net::RoomClosed{reason});
            m_endpoint->disconnect(connection);   // (playerGone reports it)
        }
    }

    // -- The end -----------------------------------------------------------------------------------
    void RoomScene::finish(net::RoomEnd reason)
    {
        if (m_finishing) {
            return;
        }
        m_finishing = true;
        m_endReason = reason;
        m_announceAt = now() + std::max(m_init.server.closeDelay, 0.0);
        m_finishAt = m_announceAt + m_init.server.linger;
        tellLobby(detail::RoomClosing{m_init.roomId});   // before the players hear of it: when they ask again, the lobby knows
        if (m_init.server.closeDelay <= 0.0) {
            announceEnd();
        }
    }

    // "Room closed" is reliable, the snapshots are not: a client that hears it lets go of the room, so
    // the room waited (closeDelay) for the last snapshots to leave
    void RoomScene::announceEnd(void)
    {
        if (m_announced) {
            return;
        }
        m_announced = true;
        broadcast(net::RoomClosed{m_endReason});
        // Whoever is connected but not a player is sent away
        for (const auto id : m_endpoint->connections()) {
            if (!player(id)) {
                m_endpoint->disconnect(id);
            }
        }
    }

    void RoomScene::completeFinish(void)
    {
        if (!m_ended) {
            m_ended = true;
            tellLobby(detail::RoomEnded{m_init.roomId, m_endReason});
        }
        ctx().scenes().pop();
    }

    void RoomScene::housekeeping(void)
    {
        const double time = now();

        if (m_finishing) {
            if (time >= m_announceAt) {
                announceEnd();
            }
            if (time >= m_finishAt) {
                completeFinish();
            }
            return;
        }
        // Hellos that came before the lobby's word: they get a moment
        for (auto it = m_waiting.begin(); it != m_waiting.end();) {
            if (admit(it->connection, it->token)) {
                it = m_waiting.erase(it);
            } else if (time - it->since > HELLO_GRACE) {
                const net::ConnectionId connection = it->connection;

                it = m_waiting.erase(it);
                reject(connection, net::HelloError::UnknownToken);
            } else {
                ++it;
            }
        }
        // A connection that never says hello is not kept
        std::vector<net::ConnectionId> silent;

        for (const auto& [connection, since] : m_unknown) {
            const bool waiting = std::any_of(m_waiting.begin(), m_waiting.end(), [connection](const WaitingHello& hello) { return hello.connection == connection; });

            if (!waiting && time - since > m_init.server.helloTimeout) {
                silent.push_back(connection);
            }
        }
        for (const auto connection : silent) {
            m_unknown.erase(connection);
            m_endpoint->disconnect(connection);
        }
        std::erase_if(m_expected, [time](const auto& entry) { return entry.second.expires < time; });
        if (m_players.empty() && m_expected.empty() && time - m_lastActive > m_init.type.idleTimeout) {
            finish(net::RoomEnd::Idle);
        }
    }

}
