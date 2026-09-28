#include "Endpoint.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <random>

namespace kuge::net
{

    namespace
    {
        constexpr std::uint32_t MAGIC = 0x4547554Bu;   // "KUGE"

        // The first byte of a packet
        enum PacketType : std::uint8_t { Connect = 1, Accept = 2, Reject = 3, Data = 4, Goodbye = 5 };

        // Data: type, channel, ack, ack bits, then (reliable) the number of the message
        constexpr std::size_t DATA_HEADER = 1 + 1 + 4 + 4;
        constexpr std::size_t RELIABLE_HEADER = DATA_HEADER + 4;
        constexpr std::uint8_t CONTROL = 2;   // a Data packet with no message: an ack, or a keep-alive

        double steadySeconds(void)
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        std::uint32_t randomSalt(void)
        {
            static std::random_device device;

            return device();
        }
    }

    Endpoint::Endpoint(std::unique_ptr<ITransport> transport, Role role)
        : Endpoint(std::move(transport), role, EndpointConfig{})
    {
    }

    Endpoint::Endpoint(std::unique_ptr<ITransport> transport, Role role, EndpointConfig config)
        : m_transport(std::move(transport)), m_role(role), m_config(std::move(config))
    {
        if (!m_transport) {
            throw std::invalid_argument("Endpoint: no transport");
        }
        m_clock = m_config.clock ? m_config.clock : std::function<double(void)>(steadySeconds);
        m_config.reliable.resend = !m_transport->reliable();
        if (m_role == Role::Client) {
            Connection connection{CLIENT_CONNECTION, State::Connecting, now(), 0.0, 0.0, 0.0, randomSalt(), false,
                ReliableChannel(m_config.reliable), m_transport->describe()};

            connection.lastReceived = connection.created;
            m_connections.emplace(CLIENT_CONNECTION, std::move(connection));
        }
    }

    Endpoint::~Endpoint(void)
    {
        const double time = now();

        *m_alive = false;
        // Whoever is connected is told: they need not wait for a timeout
        for (auto& [id, connection] : m_connections) {
            if (connection.state == State::Connected) {
                sendSimple(connection, Goodbye, 0, time);
            }
        }
    }

    void Endpoint::registerHandler(std::uint32_t id, const char* name, std::function<void(ConnectionId, ByteReader&)> run)
    {
        const auto found = m_handlers.find(id);

        if (found != m_handlers.end() && found->second.name != name) {
            throw std::logic_error(std::string("net: the messages '") + name + "' and '" + found->second.name + "' have the same id: rename one");
        }
        m_handlers[id] = Handler{name, std::make_shared<const std::function<void(ConnectionId, ByteReader&)>>(std::move(run))};
    }

    // -- Sending ---------------------------------------------------------------------------------
    void Endpoint::transmit(Connection& connection, const std::vector<std::uint8_t>& packet, double time)
    {
        if (m_transport->send(connection.id, packet)) {
            ++m_stats.packetsSent;
            m_stats.bytesSent += packet.size();
            connection.lastSent = time;
        }
    }

    void Endpoint::sendSimple(Connection& connection, std::uint8_t type, std::uint32_t value, double time)
    {
        ByteWriter out;

        out.write<std::uint8_t>(type);
        if (type == Connect) {
            out.write<std::uint32_t>(MAGIC);
            out.write<std::uint16_t>(m_config.protocol);
        }
        out.write<std::uint32_t>(value);
        transmit(connection, out.bytes(), time);
    }

    // A Data packet. Every one carries what we received (ack), so acks flow with the traffic.
    void Endpoint::sendData(Connection& connection, Channel channel, bool control, std::uint32_t seq,
        const std::vector<std::uint8_t>* payload, double time)
    {
        ByteWriter out;

        out.write<std::uint8_t>(Data);
        out.write<std::uint8_t>(control ? CONTROL : static_cast<std::uint8_t>(channel));
        out.write<std::uint32_t>(connection.reliable.ack());
        out.write<std::uint32_t>(connection.reliable.ackBits());
        if (!control && channel == Channel::Reliable) {
            out.write<std::uint32_t>(seq);
        }
        if (payload) {
            out.writeBytes(*payload);
        }
        connection.reliable.ackSent();
        transmit(connection, out.bytes(), time);
    }

    void Endpoint::flushReliable(Connection& connection, double time)
    {
        std::vector<ReliableChannel::Outgoing> out;

        connection.reliable.collect(time, out);
        for (const auto& message : out) {
            sendData(connection, Channel::Reliable, false, message.seq, message.payload, time);
        }
    }

    bool Endpoint::sendPayload(ConnectionId to, const std::vector<std::uint8_t>& payload, Channel channel)
    {
        const auto found = m_connections.find(to);
        const std::size_t header = channel == Channel::Reliable ? RELIABLE_HEADER : DATA_HEADER;

        if (found == m_connections.end() || found->second.state != State::Connected || payload.size() + header > MAX_PACKET) {
            ++m_stats.refusedSends;
            return false;
        }
        Connection& connection = found->second;
        const double time = now();

        if (channel == Channel::Reliable) {
            if (!connection.reliable.queue(payload)) {
                ++m_stats.refusedSends;
                return false;
            }
            flushReliable(connection, time);
        } else {
            sendData(connection, Channel::Unreliable, false, 0, &payload, time);
        }
        ++m_stats.messagesSent;
        return true;
    }

    // -- Connections -----------------------------------------------------------------------------
    void Endpoint::establish(Connection& connection)
    {
        const ConnectionId id = connection.id;

        connection.state = State::Connected;
        // (A copy runs: the handler may destroy this endpoint, and itself with it)
        m_deferred.push_back([this, id] { if (const ConnectedHandler handler = m_onConnected) { handler(id); } });
    }

    void Endpoint::close(ConnectionId id, DisconnectReason reason, bool tellPeer, bool closeTransport)
    {
        const auto found = m_connections.find(id);

        if (found == m_connections.end()) {
            return;
        }
        const bool wasConnected = found->second.state == State::Connected;

        if (tellPeer && wasConnected) {
            sendSimple(found->second, Goodbye, 0, now());
        }
        m_closedResends += found->second.reliable.resends();
        m_closedDuplicates += found->second.reliable.duplicates();
        m_connections.erase(found);
        if (closeTransport) {
            m_transport->close(id);
        }
        if (m_role == Role::Client) {
            m_clientClosed = true;
        }
        // A server does not report a connection that never got through the handshake
        if (wasConnected || m_role == Role::Client) {
            m_deferred.push_back([this, id, reason] { if (const DisconnectedHandler handler = m_onDisconnected) { handler(id, reason); } });
        }
    }

    void Endpoint::disconnect(ConnectionId connection)
    {
        close(connection, DisconnectReason::Local, true, true);
        if (!m_inPoll) {
            runDeferred();
        }
    }

    bool Endpoint::connected(void) const
    {
        return std::any_of(m_connections.begin(), m_connections.end(), [](const auto& entry) { return entry.second.state == State::Connected; });
    }

    std::vector<ConnectionId> Endpoint::connections(void) const
    {
        std::vector<ConnectionId> ids;

        for (const auto& [id, connection] : m_connections) {
            if (connection.state == State::Connected) {
                ids.push_back(id);
            }
        }
        return ids;
    }

    EndpointStats Endpoint::stats(void) const
    {
        EndpointStats stats = m_stats;

        stats.resends = m_closedResends;
        stats.duplicates = m_closedDuplicates;
        for (const auto& [id, connection] : m_connections) {
            stats.resends += connection.reliable.resends();
            stats.duplicates += connection.reliable.duplicates();
        }
        return stats;
    }

    std::optional<double> Endpoint::rtt(ConnectionId id) const
    {
        const auto found = m_connections.find(id);

        if (found == m_connections.end() || found->second.reliable.rtt() <= 0.0) {
            return std::nullopt;
        }
        return found->second.reliable.rtt();
    }

    std::string Endpoint::peer(ConnectionId id) const
    {
        const auto found = m_connections.find(id);

        return found == m_connections.end() ? std::string() : found->second.peer;
    }

    // -- Receiving -------------------------------------------------------------------------------
    // The handler runs later, when the endpoint is in order (see runDeferred)
    void Endpoint::deferMessage(ConnectionId from, std::span<const std::uint8_t> message)
    {
        m_deferred.push_back([this, from, bytes = std::vector<std::uint8_t>(message.begin(), message.end())] {
            handleMessage(from, bytes);
        });
    }

    void Endpoint::handleMessage(ConnectionId from, std::span<const std::uint8_t> message)
    {
        const std::shared_ptr<bool> alive = m_alive;

        try {
            ByteReader in(message);
            const std::uint32_t id = in.read<std::uint32_t>();
            const auto found = m_handlers.find(id);

            ++m_stats.messagesReceived;
            if (found == m_handlers.end()) {
                ++m_stats.unknownMessages;
                return;
            }
            const auto run = found->second.run;   // (a copy: the handler may destroy this endpoint, or replace itself)

            (*run)(from, in);
        } catch (const SerializerError& error) {
            if (*alive) {
                ++m_stats.malformed;
            }
            Logger::logger().warn("net: a malformed message from connection {} was dropped: {}", from, error.what());
        }
    }

    void Endpoint::handlePacket(Connection& connection, std::span<const std::uint8_t> packet, double time)
    {
        ByteReader in(packet);
        const ConnectionId id = connection.id;

        try {
            const std::uint8_t type = in.read<std::uint8_t>();

            switch (type) {
                case Connect: {
                    if (m_role != Role::Server || in.read<std::uint32_t>() != MAGIC) {
                        ++m_stats.malformed;
                        return;
                    }
                    const std::uint16_t version = in.read<std::uint16_t>();
                    const std::uint32_t salt = in.read<std::uint32_t>();

                    connection.lastReceived = time;
                    if (version != m_config.protocol) {
                        sendSimple(connection, Reject, 0, time);
                        close(id, DisconnectReason::Refused, false, true);
                        return;
                    }
                    if (connection.state == State::Handshaking) {
                        connection.salt = salt;
                        establish(connection);
                    }
                    if (connection.state == State::Connected && connection.salt == salt) {
                        sendSimple(connection, Accept, salt, time);   // (again, if the first one was lost)
                    }
                    return;
                }
                case Accept: {
                    const std::uint32_t salt = in.read<std::uint32_t>();

                    if (m_role == Role::Client && connection.state == State::Connecting && salt == connection.salt) {
                        connection.lastReceived = time;
                        establish(connection);
                    } else if (connection.state == State::Connected) {
                        connection.lastReceived = time;
                    }
                    return;
                }
                case Reject:
                    if (m_role == Role::Client) {
                        close(id, DisconnectReason::Refused, false, true);
                    }
                    return;
                case Goodbye:
                    close(id, DisconnectReason::Remote, false, true);
                    return;
                case Data: {
                    // A client that gets Data before Accept knows the server sees it as connected: the Accept was lost
                    if (connection.state == State::Connecting && m_role == Role::Client) {
                        establish(connection);
                    }
                    if (connection.state != State::Connected) {
                        return;
                    }
                    connection.lastReceived = time;
                    const std::uint8_t channel = in.read<std::uint8_t>();
                    const std::uint32_t ack = in.read<std::uint32_t>();
                    const std::uint32_t bits = in.read<std::uint32_t>();

                    connection.reliable.onAck(ack, bits, time);
                    if (channel == static_cast<std::uint8_t>(Channel::Unreliable)) {
                        deferMessage(id, packet.subspan(in.position()));
                    } else if (channel == static_cast<std::uint8_t>(Channel::Reliable)) {
                        const std::uint32_t seq = in.read<std::uint32_t>();
                        const auto body = packet.subspan(in.position());

                        connection.reliable.onData(seq, std::vector<std::uint8_t>(body.begin(), body.end()));
                        for (const auto& message : connection.reliable.takeDelivered()) {
                            deferMessage(id, message);
                        }
                    } else if (channel != CONTROL) {
                        ++m_stats.malformed;
                    }
                    return;
                }
                default:
                    ++m_stats.malformed;
                    return;
            }
        } catch (const SerializerError&) {
            ++m_stats.malformed;
        }
    }

    void Endpoint::handleEvent(TransportEvent& event, double time)
    {
        switch (event.kind) {
            case TransportEvent::Kind::Connected: {
                if (m_role == Role::Client) {
                    const auto found = m_connections.find(event.connection);

                    if (found != m_connections.end()) {
                        found->second.transportUp = true;
                        found->second.nextConnect = time;
                        if (!event.peer.empty()) {
                            found->second.peer = event.peer;
                        }
                    }
                } else if (m_connections.size() >= m_config.maxConnections) {
                    m_transport->close(event.connection);   // no room
                } else {
                    Connection connection{event.connection, State::Handshaking, time, time, 0.0, 0.0, 0, true,
                        ReliableChannel(m_config.reliable), event.peer};

                    m_connections.emplace(event.connection, std::move(connection));
                }
                return;
            }
            case TransportEvent::Kind::Disconnected: {
                const auto found = m_connections.find(event.connection);

                if (found != m_connections.end()) {
                    close(event.connection, DisconnectReason::Unreachable, false, false);
                }
                return;
            }
            case TransportEvent::Kind::Packet: {
                const auto found = m_connections.find(event.connection);

                ++m_stats.packetsReceived;
                m_stats.bytesReceived += event.packet.size();
                if (found != m_connections.end()) {
                    handlePacket(found->second, event.packet, time);
                }
                return;
            }
        }
    }

    // What a connection does with the time: connecting again, sending what is due, timing out
    void Endpoint::service(Connection& connection, double time)
    {
        switch (connection.state) {
            case State::Connecting:
                if (time - connection.created >= m_config.connectTimeout) {
                    close(connection.id, DisconnectReason::Timeout, false, true);
                } else if (connection.transportUp && time >= connection.nextConnect) {
                    connection.nextConnect = time + m_config.connectRetry;
                    sendSimple(connection, Connect, connection.salt, time);
                }
                return;
            case State::Handshaking:
                if (time - connection.created >= m_config.connectTimeout) {
                    close(connection.id, DisconnectReason::Timeout, false, true);   // (a peer that never says hello)
                }
                return;
            case State::Connected:
                if (time - connection.lastReceived >= m_config.timeout) {
                    close(connection.id, DisconnectReason::Timeout, true, true);
                    return;
                }
                flushReliable(connection, time);
                if (connection.reliable.ackPending() || time - connection.lastSent >= m_config.keepAlive) {
                    sendData(connection, Channel::Unreliable, true, 0, nullptr, time);
                }
                return;
        }
    }

    void Endpoint::poll(void)
    {
        std::vector<TransportEvent> events;
        const double time = now();

        m_inPoll = true;
        m_transport->poll(events);
        for (auto& event : events) {
            handleEvent(event, time);
        }
        // (A connection can be closed while serving: work on a copy of the ids)
        std::vector<ConnectionId> ids;

        for (const auto& [id, connection] : m_connections) {
            ids.push_back(id);
        }
        for (const ConnectionId id : ids) {
            const auto found = m_connections.find(id);

            if (found != m_connections.end()) {
                service(found->second, time);
            }
        }
        m_inPoll = false;
        runDeferred();
    }

    // The handlers run when the endpoint is in order: they may do anything to it, destroy it included
    void Endpoint::runDeferred(void)
    {
        const std::shared_ptr<bool> alive = m_alive;

        while (!m_deferred.empty()) {
            std::vector<std::function<void(void)>> run;

            run.swap(m_deferred);
            for (auto& job : run) {
                job();
                if (!*alive) {
                    return;   // a handler destroyed this endpoint: nothing of it may be touched, and what is left is dropped
                }
            }
        }
    }

}
