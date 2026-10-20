#include "Endpoint.hpp"
#include "SimulatedTransport.hpp"
#include "Auth.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <random>

namespace kuge::net
{

    namespace
    {
        constexpr std::uint32_t MAGIC = 0x4547554Bu;   // "KUGE"

        // The first byte of a packet
        enum PacketType : std::uint8_t { Connect = 1, Accept = 2, Reject = 3, Data = 4, Goodbye = 5, Challenge = 6 };

        // Data: type, channel, ack, ack bits, then (reliable) the number of the message, then the message, then the tag
        constexpr std::size_t DATA_HEADER = 1 + 1 + 4 + 4;
        constexpr std::size_t RELIABLE_HEADER = DATA_HEADER + 4;
        constexpr std::size_t TAG_SIZE = 4;   // the end of every Data and Goodbye packet: see tagOf()
        constexpr std::uint8_t CONTROL = 2;   // a Data packet with no message: an ack, or a keep-alive

        // The tag of a packet from the server is not the tag of the same bytes from a client: a packet is not sent back to its sender
        constexpr std::uint64_t FROM_SERVER = 0x5352565253525652ull;

        // The addresses that the server counts Connect packets of: bounded, so that spoofed ones cannot grow it
        constexpr std::size_t MAX_COUNTED_ADDRESSES = 4096;

        double steadySeconds(void)
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        // 64 bits from the system's source of randomness: the salt of a client, and the secret of a server. (Not
        // seeded: they are what an attacker must not be able to guess.)
        std::uint64_t randomWord(void)
        {
            static thread_local std::random_device device;

            return (static_cast<std::uint64_t>(device()) << 32) | device();
        }

        // "10.0.0.7:4242" and "[::1]:4242" are the address 10.0.0.7 and [::1]; a peer with no port (the loopback's) is its own
        std::string addressOf(const std::string& peer)
        {
            const auto colon = peer.rfind(':');

            return colon == std::string::npos ? peer : peer.substr(0, colon);
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
        if (m_config.simulate) {
            setSimulation(m_config.simulate);
        }
        m_secret[0] = randomWord();
        m_secret[1] = randomWord();
        if (m_role == Role::Client) {
            Connection connection{CLIENT_CONNECTION, State::Connecting, now(), 0.0, 0.0, 0.0, randomWord(), 0, false, false,
                ReliableChannel(m_config.reliable), m_transport->describe()};

            connection.lastReceived = connection.created;
            m_connections.emplace(CLIENT_CONNECTION, std::move(connection));
        }
    }

    void Endpoint::setSimulation(const std::optional<LoopbackNetwork::Conditions>& conditions)
    {
        if (!m_simulated) {
            if (!conditions) {
                return;
            }
            // The endpoint's own clock, so that a test with a made-up one has a bad network in its time
            auto wrapper = std::make_unique<SimulatedTransport>(std::move(m_transport), *conditions, m_clock);

            m_simulated = wrapper.get();
            m_transport = std::move(wrapper);
            return;
        }
        m_simulated->setConditions(conditions ? *conditions : LoopbackNetwork::Conditions{});
    }

    Endpoint::~Endpoint(void)
    {
        const double time = now();

        *m_alive = false;
        // Whoever is connected is told: they need not wait for a timeout
        for (auto& [id, connection] : m_connections) {
            if (connection.state == State::Connected) {
                sendGoodbye(connection, time);
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
            if (packet.size() > DATAGRAM_PAYLOAD && !m_transport->reliable()) {
                ++m_stats.bigPackets;
            }
            ++m_stats.packetsSent;
            m_stats.bytesSent += packet.size();
            connection.lastSent = time;
        }
    }

    // -- The handshake -------------------------------------------------------------------------
    //
    //   client                                          server
    //     |-- Connect(magic, version, salt, cookie 0) ---> |  (23 bytes)   nothing is kept for it
    //     |<-- Challenge(salt, cookie) ------------------- |  (17 bytes)   cookie = MAC(secret, address, salt, time)
    //     |-- Connect(magic, version, salt, cookie) -----> |  the address answered: the connection is made
    //     |<-- Accept(salt, cookie) ---------------------- |  (17 bytes)
    //
    // Whatever the server answers to a Connect is smaller than the Connect, so an address that is spoofed gets less
    // than what was sent in its name, and the server keeps no state for it. The salt and the cookie are the two halves
    // of the key of the packets that follow (see tagOf()). Over TCP the address is proved by TCP itself: no Challenge.
    void Endpoint::sendConnect(Connection& connection, double time)
    {
        ByteWriter out;

        out.write<std::uint8_t>(Connect);
        out.write<std::uint32_t>(MAGIC);
        out.write<std::uint16_t>(m_config.protocol);
        out.write<std::uint64_t>(connection.salt);
        out.write<std::uint64_t>(connection.haveCookie ? connection.serverSalt : 0);
        transmit(connection, out.bytes(), time);
    }

    void Endpoint::sendChallenge(Connection& connection, std::uint64_t salt, std::uint64_t cookie, double time)
    {
        ByteWriter out;

        out.write<std::uint8_t>(Challenge);
        out.write<std::uint64_t>(salt);
        out.write<std::uint64_t>(cookie);
        ++m_stats.challenges;
        transmit(connection, out.bytes(), time);
    }

    void Endpoint::sendAccept(Connection& connection, double time)
    {
        ByteWriter out;

        out.write<std::uint8_t>(Accept);
        out.write<std::uint64_t>(connection.salt);
        out.write<std::uint64_t>(connection.serverSalt);
        transmit(connection, out.bytes(), time);
    }

    // (Not authenticated, and as small as can be: a refusal is also the answer to an address that proved nothing)
    void Endpoint::sendReject(Connection& connection, double time)
    {
        ByteWriter out;

        out.write<std::uint8_t>(Reject);
        out.write<std::uint32_t>(0);
        transmit(connection, out.bytes(), time);
    }

    void Endpoint::sendGoodbye(Connection& connection, double time)
    {
        ByteWriter out;

        out.write<std::uint8_t>(Goodbye);
        seal(connection, out);
        transmit(connection, out.bytes(), time);
    }

    // -- The tag of a packet ---------------------------------------------------------------------
    // Every Data and Goodbye packet ends with 4 bytes: the SipHash of the rest, with a key made of the client's salt and
    // the server's cookie, which the two know and a third party that does not see the handshake does not. A packet
    // that does not carry the right tag is dropped (counted in `malformed`). It stops an attacker who is not on the
    // path from injecting a packet in a connection by guessing its address and sequence; it does not stop one who reads
    // the handshake, and it hides nothing (nothing is encrypted).
    std::uint32_t Endpoint::tagOf(const Connection& connection, std::span<const std::uint8_t> body, bool fromServer) const
    {
        return static_cast<std::uint32_t>(sipHash24(connection.salt ^ (fromServer ? FROM_SERVER : 0), connection.serverSalt, body));
    }

    void Endpoint::seal(const Connection& connection, ByteWriter& out) const
    {
        out.write<std::uint32_t>(tagOf(connection, out.bytes(), m_role == Role::Server));
    }

    // Is the tag at the end of this packet the one that the other side would have put?
    bool Endpoint::opens(const Connection& connection, std::span<const std::uint8_t> packet) const
    {
        if (packet.size() < 1 + TAG_SIZE) {
            return false;
        }
        const auto body = packet.first(packet.size() - TAG_SIZE);
        std::uint32_t tag = 0;

        for (std::size_t i = 0; i < TAG_SIZE; ++i) {
            tag |= static_cast<std::uint32_t>(packet[body.size() + i]) << (8 * i);
        }
        return tag == tagOf(connection, body, m_role == Role::Client);
    }

    // What proves that an address receives what is sent to it: only a cookie that the server made can be sent back. It
    // changes every cookieWindow seconds; the previous one is still good (see the Connect case of handlePacket).
    std::uint64_t Endpoint::cookieOf(const std::string& peer, std::uint64_t salt, std::uint64_t window) const
    {
        ByteWriter in;

        in.write<std::uint64_t>(window);
        in.write<std::uint64_t>(salt);
        in.writeBytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(peer.data()), peer.size()));
        return sipHash24(m_secret[0], m_secret[1], in.bytes());
    }

    // Connect packets from one address, in windows of a second. Bounded: when the table is full of recent addresses,
    // a new one is not counted (a flood of thousands of distinct addresses is held by the cookie and maxHalfOpen, not by this).
    bool Endpoint::allowConnect(const std::string& peer, double time)
    {
        if (m_config.connectsPerSecond == 0) {
            return true;
        }
        if (m_connects.size() >= MAX_COUNTED_ADDRESSES) {
            std::erase_if(m_connects, [time](const auto& entry) { return time - entry.second.start >= 1.0; });
        }
        const std::string address = addressOf(peer);
        auto found = m_connects.find(address);

        if (found == m_connects.end()) {
            if (m_connects.size() >= MAX_COUNTED_ADDRESSES) {
                return true;
            }
            found = m_connects.emplace(address, ConnectCount{time, 0}).first;
        } else if (time - found->second.start >= 1.0) {
            found->second = ConnectCount{time, 0};
        }
        return ++found->second.count <= m_config.connectsPerSecond;
    }

    std::size_t Endpoint::halfOpen(void) const
    {
        return static_cast<std::size_t>(std::count_if(m_connections.begin(), m_connections.end(),
            [](const auto& entry) { return entry.second.state == State::Handshaking; }));
    }

    std::size_t Endpoint::established(void) const
    {
        return static_cast<std::size_t>(std::count_if(m_connections.begin(), m_connections.end(),
            [](const auto& entry) { return entry.second.state == State::Connected; }));
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
        seal(connection, out);
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

        if (found == m_connections.end() || found->second.state != State::Connected || payload.size() + header + TAG_SIZE > MAX_PACKET) {
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
            sendGoodbye(found->second, now());
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
            Logger::logger().category("net").warn("net: a malformed message from connection {} was dropped: {}", from, error.what());
        }
    }

    void Endpoint::handlePacket(Connection& connection, std::span<const std::uint8_t> packet, double time)
    {
        const ConnectionId id = connection.id;

        if (packet.empty()) {
            ++m_stats.malformed;
            return;
        }
        try {
            const std::uint8_t type = packet[0];
            ByteReader in(packet.subspan(1));

            switch (type) {
                case Connect: {
                    if (m_role != Role::Server || in.read<std::uint32_t>() != MAGIC) {
                        ++m_stats.malformed;
                        return;
                    }
                    // The version first: what follows is not the same in another one
                    if (in.read<std::uint16_t>() != m_config.protocol) {
                        sendReject(connection, time);
                        close(id, DisconnectReason::Refused, false, true);
                        return;
                    }
                    const std::uint64_t salt = in.read<std::uint64_t>();
                    const std::uint64_t cookie = in.read<std::uint64_t>();

                    if (!allowConnect(connection.peer, time)) {
                        ++m_stats.rateLimited;
                        return;
                    }
                    if (connection.state == State::Connected) {
                        // The Accept was lost: the client asks again with the same salt and cookie (a stranger knows neither)
                        if (salt == connection.salt && cookie == connection.serverSalt) {
                            connection.lastReceived = time;
                            sendAccept(connection, time);
                        }
                        return;
                    }
                    // This address has said that it is there. Has it also heard us? Only a cookie that we made, for this
                    // address and this salt, in this window or the last, can come back. Nothing is kept for the ones that do not.
                    const auto window = static_cast<std::uint64_t>(time / std::max(m_config.cookieWindow, 0.001));
                    const std::uint64_t current = cookieOf(connection.peer, salt, window);
                    const bool tcp = m_transport->reliable();   // (TCP has proved the address already)
                    const bool proved = tcp || cookie == current || (window > 0 && cookie == cookieOf(connection.peer, salt, window - 1));

                    if (!proved) {
                        sendChallenge(connection, salt, current, time);
                        return;
                    }
                    if (established() >= m_config.maxConnections) {
                        sendReject(connection, time);
                        close(id, DisconnectReason::Refused, false, true);
                        return;
                    }
                    connection.salt = salt;
                    connection.serverSalt = tcp ? current : cookie;
                    connection.lastReceived = time;
                    establish(connection);
                    sendAccept(connection, time);
                    return;
                }
                case Challenge: {
                    const std::uint64_t salt = in.read<std::uint64_t>();
                    const std::uint64_t cookie = in.read<std::uint64_t>();

                    // (Again when the cookie ran out: the server sends a new one, and the next Connect carries it)
                    if (m_role == Role::Client && connection.state == State::Connecting && salt == connection.salt) {
                        connection.serverSalt = cookie;
                        connection.haveCookie = true;
                        connection.nextConnect = time;
                    }
                    return;
                }
                case Accept: {
                    const std::uint64_t salt = in.read<std::uint64_t>();
                    const std::uint64_t cookie = in.read<std::uint64_t>();

                    if (m_role == Role::Client && connection.state == State::Connecting && salt == connection.salt) {
                        connection.serverSalt = cookie;
                        connection.haveCookie = true;
                        connection.lastReceived = time;
                        establish(connection);
                    }
                    return;   // (A copy of the Accept, once connected, proves nothing and keeps nothing alive)
                }
                case Reject:
                    // Only while connecting: it has no tag, so it must not end a connection that is made
                    if (m_role == Role::Client && connection.state == State::Connecting) {
                        close(id, DisconnectReason::Refused, false, true);
                    }
                    return;
                case Goodbye:
                    if (connection.state == State::Connected && opens(connection, packet)) {
                        close(id, DisconnectReason::Remote, false, true);
                    } else {
                        ++m_stats.malformed;
                    }
                    return;
                case Data: {
                    // A client that gets Data before Accept knows the server sees it as connected: the Accept was lost.
                    // (It has the key already, from the Challenge: the packet shows whether it is the server's.)
                    if (connection.state == State::Connecting && m_role == Role::Client && connection.haveCookie && opens(connection, packet)) {
                        establish(connection);
                    }
                    if (connection.state != State::Connected) {
                        return;
                    }
                    if (!opens(connection, packet)) {
                        ++m_stats.malformed;   // not from the other end of this connection
                        return;
                    }
                    const auto body = packet.first(packet.size() - TAG_SIZE);
                    ByteReader data(body.subspan(1));

                    connection.lastReceived = time;
                    const std::uint8_t channel = data.read<std::uint8_t>();
                    const std::uint32_t ack = data.read<std::uint32_t>();
                    const std::uint32_t bits = data.read<std::uint32_t>();

                    connection.reliable.onAck(ack, bits, time);
                    if (channel == static_cast<std::uint8_t>(Channel::Unreliable)) {
                        deferMessage(id, body.subspan(1 + data.position()));
                    } else if (channel == static_cast<std::uint8_t>(Channel::Reliable)) {
                        const std::uint32_t seq = data.read<std::uint32_t>();
                        const auto message = body.subspan(1 + data.position());

                        connection.reliable.onData(seq, std::vector<std::uint8_t>(message.begin(), message.end()));
                        for (const auto& delivered : connection.reliable.takeDelivered()) {
                            deferMessage(id, delivered);
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
                } else {
                    // A peer that has not yet proved its address (see the handshake) takes no place of maxConnections, only
                    // one of the maxHalfOpen: when they are all taken, the oldest goes (a flood of Connect from addresses that
                    // never answer must not keep the real ones out, and does not grow without end).
                    std::size_t halfOpen = 0;
                    const Connection* oldest = nullptr;

                    for (const auto& [id, other] : m_connections) {
                        if (other.state == State::Handshaking) {
                            ++halfOpen;
                            if (!oldest || other.created < oldest->created) {
                                oldest = &other;
                            }
                        }
                    }
                    if (halfOpen >= std::max<std::size_t>(m_config.maxHalfOpen, 1) && oldest) {
                        close(oldest->id, DisconnectReason::Unreachable, false, true);   // (not reported: it never got through)
                    }
                    Connection connection{event.connection, State::Handshaking, time, time, 0.0, 0.0, 0, 0, false, true,
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
                    sendConnect(connection, time);
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
