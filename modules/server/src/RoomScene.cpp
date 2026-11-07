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
            Logger::logger().category("server").error("room {}: cannot listen: {}", m_init.roomId, error.what());
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
        on<net::CinematicDone>([this](const Player& who, const net::CinematicDone& done) { cinematicDone(who.networkId, done.id); });
        onRoomEnter();
        tellLobby(detail::RoomReady{m_init.roomId});
        Logger::logger().category("server").info("room {} ({}) is open", m_init.roomId, m_init.roomType);
    }

    void RoomScene::onExit(void)
    {
        onRoomExit();
        if (!m_ended) {
            m_ended = true;
            if (!m_finishing) {
                m_endReason = net::RoomEnd::ServerStopping;
            }
            // The server is stopping: the players hear why from here, not only from the lobby, which goes in the same
            // pass (the endpoint sends at once, so nothing needs to poll after this)
            if (m_endpoint) {
                announceEnd();
            }
            tellLobby(detail::RoomEnded{m_init.roomId, m_endReason});
        }
        Logger::logger().category("server").info("room {} is closed", m_init.roomId);
    }

    void RoomScene::onMessage(const Message& message)
    {
        if (const auto* expect = message.as<detail::ExpectPlayer>()) {
            m_expected[expect->token] = Expected{expect->name, expect->playerId, now() + m_init.server.tokenTtl, expect->rejoin};
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
        const Expected expected = found->second;

        m_expected.erase(found);        // a token opens the door once
        m_unknown.erase(from);
        // A player that comes back takes its place again: the same Player, the same network id
        if (expected.rejoin) {
            for (auto& [networkId, who] : m_players) {
                if (who.playerId != expected.playerId) {
                    continue;
                }
                const net::ConnectionId previous = who.connection;

                if (!who.away && previous != 0) {
                    m_endpoint->disconnect(previous);   // the room had not noticed the loss yet: the old connection goes
                }
                who.connection = from;
                who.address = net::addressOf(m_endpoint->peer(from));
                who.away = false;
                m_lastActive = now();
                m_endpoint->send(from, net::Welcome{who.networkId, m_init.roomId, m_init.server.tickRate});
                tellLobby(detail::PlayerJoined{m_init.roomId, who.playerId});
                onPlayerRejoined(who, previous);
                return true;
            }
            // Its place is gone (the time ran out): it is a new player, like any other
        }
        Player who{expected.playerId, expected.name, m_nextNetworkId++, from, net::addressOf(m_endpoint->peer(from))};

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

    void RoomScene::enableChat(ChatConfig config)
    {
        m_chat = std::make_unique<ChatRelay>(config);
        on<net::ChatSend>([this](const Player& who, const net::ChatSend& said) {
            auto line = m_chat->accept(who.connection, said.text, now());

            if (!line) {
                Logger::logger().category("server").debug("room {}: chat from player {} refused", m_init.roomId, who.networkId);
                return;
            }
            if (auto relayed = onChatLine(who, std::move(*line))) {
                broadcast(net::ChatLine{who.networkId, std::move(*relayed)});
            }
        });
    }

    bool RoomScene::playCinematic(const std::string& id, double timeLimit)
    {
        if (m_cinematic || m_finishing) {
            return false;
        }
        m_cinematic = ActiveCinematic{id, {}, now() + timeLimit};
        for (const auto& [networkId, who] : m_players) {
            if (!who.away) {
                m_cinematic->waiting.insert(networkId);
            }
        }
        broadcast(net::CinematicStart{id});
        if (m_cinematic->waiting.empty()) {
            endCinematic(true);
        }
        return true;
    }

    // A player said it is done, or is gone
    void RoomScene::cinematicDone(std::uint32_t networkId, const std::string& id)
    {
        if (m_cinematic && (id.empty() || m_cinematic->id == id) && m_cinematic->waiting.erase(networkId) && m_cinematic->waiting.empty()) {
            endCinematic(true);
        }
    }

    void RoomScene::endCinematic(bool completed)
    {
        const ActiveCinematic over = std::move(*m_cinematic);

        m_cinematic.reset();
        for (const std::uint32_t networkId : over.waiting) {
            send(networkId, net::CinematicStop{over.id});
        }
        onCinematicEnded(over.id, completed);
    }

    void RoomScene::playerGone(net::ConnectionId connection, net::DisconnectReason reason)
    {
        if (m_chat) {
            m_chat->forget(connection);
        }
        m_unknown.erase(connection);
        std::erase_if(m_waiting, [connection](const WaitingHello& hello) { return hello.connection == connection; });
        for (auto it = m_players.begin(); it != m_players.end(); ++it) {
            if (it->second.connection == connection && !it->second.away) {
                cinematicDone(it->first, {});
                // A lost connection (not one the player ended, or that the room did) is kept for a while if the room type asks
                const bool lost = reason == net::DisconnectReason::Timeout || reason == net::DisconnectReason::Unreachable;

                if (lost && m_init.type.reconnectSeconds > 0.0 && !m_finishing) {
                    it->second.away = true;
                    it->second.awaySince = now();
                    it->second.connection = 0;
                    m_lastActive = now();
                    onPlayerAway(it->second, reason);
                    return;
                }
                letGo(it->first, reason);
                return;
            }
        }
    }

    // A player is out for good: its place is free, the lobby knows
    void RoomScene::letGo(std::uint32_t networkId, net::DisconnectReason reason)
    {
        const auto it = m_players.find(networkId);

        if (it == m_players.end()) {
            return;
        }
        const Player who = it->second;

        m_players.erase(it);
        m_lastActive = now();
        tellLobby(detail::PlayerLeft{m_init.roomId, who.playerId});
        onPlayerLeft(who, reason);
    }

    bool RoomScene::setTeam(std::uint32_t networkId, std::uint32_t team)
    {
        const auto it = m_players.find(networkId);

        if (it == m_players.end()) {
            return false;
        }
        it->second.team = team;
        return true;
    }

    void RoomScene::kick(std::uint32_t networkId, net::RoomEnd reason)
    {
        const Player* who = playerByNetworkId(networkId);

        if (who && who->away) {
            letGo(networkId, net::DisconnectReason::Local);   // nobody to tell: its place just goes
        } else if (who) {
            const net::ConnectionId connection = who->connection;

            m_endpoint->send(connection, net::RoomClosed{reason});
            m_endpoint->disconnect(connection);   // (playerGone reports it)
        }
    }

    void RoomScene::setOpen(bool open)
    {
        if (open != m_open) {
            m_open = open;
            tellLobby(detail::RoomOpen{m_init.roomId, open});
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
        tellLobby(detail::RoomClosing{m_init.roomId, reason});   // before the players hear of it: when they ask again, the lobby knows
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

        // A player that stayed away longer than the room keeps its place is gone
        if (m_init.type.reconnectSeconds > 0.0) {
            std::vector<std::uint32_t> expired;

            for (const auto& [networkId, who] : m_players) {
                if (who.away && time - who.awaySince >= m_init.type.reconnectSeconds) {
                    expired.push_back(networkId);
                }
            }
            for (const std::uint32_t networkId : expired) {
                letGo(networkId, net::DisconnectReason::Timeout);
            }
        }
        // The numbers of the network, in the log: the same line for every game (see NetMeter and describe())
        if (m_init.server.statsEvery > 0.0 && time >= m_nextStats) {
            const NetReport& report = m_meter.update(*m_endpoint, std::nullopt, time, m_players.size());

            if (m_nextStats > 0.0 && !m_players.empty()) {
                Logger::logger().category("server").info("room {} ({}): {} players, {}", m_init.roomId, m_init.roomName, m_players.size(), describe(report));
            }
            m_nextStats = time + m_init.server.statsEvery;
        }

        if (m_cinematic && time >= m_cinematic->deadline) {
            Logger::logger().category("server").warn("room {}: cinematic '{}' cut after its time limit ({} players still playing)", m_init.roomId, m_cinematic->id,
                m_cinematic->waiting.size());
            endCinematic(false);
        }
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
