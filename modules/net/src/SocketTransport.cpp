#include "SocketTransport.hpp"
#include "Logger.hpp"
#include <map>
#include <mutex>
#include <stdexcept>
extern "C" {
    #include "kronknet/client/client.h"
    #include "kronknet/connection/connection.h"
    #include "kronknet/macros/errdef.h"
    #include "kronknet/macros/types.h"
    #include "kronknet/server/server.h"
}

namespace kuge::net
{

    namespace
    {
        // kronknet keeps a counter for the connections that is shared by the whole process and not
        // protected: everything that reaches it (making, polling, destroying) goes through this
        std::mutex g_kronknet;

        constexpr std::size_t FRAME_HEADER = 4;
        constexpr int         MAX_READS_PER_POLL = 512;   //!< Datagrams or chunks taken from a socket in one poll

        // "127.0.0.1:4242", or "[::1]:4242": an IPv6 address has colons of its own
        std::string peerName(const knConnection* conn)
        {
            const std::string ip = knConnection_getIp(conn);
            const std::string port = std::to_string(knConnection_getPort(conn));

            return ip.find(':') == std::string::npos ? ip + ":" + port : "[" + ip + "]:" + port;
        }
    }

    // -- StreamFramer -------------------------------------------------------------------------
    std::vector<std::uint8_t> StreamFramer::frame(std::span<const std::uint8_t> packet)
    {
        std::vector<std::uint8_t> framed;
        const auto size = static_cast<std::uint32_t>(packet.size());

        framed.reserve(FRAME_HEADER + packet.size());
        for (int shift = 0; shift < 32; shift += 8) {
            framed.push_back(static_cast<std::uint8_t>(size >> shift));
        }
        framed.insert(framed.end(), packet.begin(), packet.end());
        return framed;
    }

    void StreamFramer::push(std::span<const std::uint8_t> bytes)
    {
        // What was read is dropped from time to time, not at each packet
        if (m_start > 0 && m_start >= m_buffer.size() / 2) {
            m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(m_start));
            m_start = 0;
        }
        m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
    }

    bool StreamFramer::next(std::vector<std::uint8_t>& packet)
    {
        if (pending() < FRAME_HEADER) {
            return false;
        }
        const std::uint8_t* head = m_buffer.data() + m_start;
        const std::uint32_t size = head[0] | head[1] << 8 | head[2] << 16 | static_cast<std::uint32_t>(head[3]) << 24;

        if (size == 0 || size > MAX_PACKET) {
            throw std::length_error("a packet of " + std::to_string(size) + " bytes on the stream");
        }
        if (pending() < FRAME_HEADER + size) {
            return false;
        }
        packet.assign(head + FRAME_HEADER, head + FRAME_HEADER + size);
        m_start += FRAME_HEADER + size;
        if (m_start == m_buffer.size()) {
            m_buffer.clear();
            m_start = 0;
        }
        return true;
    }

    namespace
    {
        // -- Server ---------------------------------------------------------------------------
        class SocketServer final : public ITransport
        {
            public:
                SocketServer(std::uint16_t port, bool tcp) : m_tcp(tcp), m_port(port)
                {
                    std::lock_guard lock(g_kronknet);

                    m_server = knServer_create(port, tcp ? knTCP : knUDP);
                    if (!m_server) {
                        throw std::runtime_error(std::string("net: cannot listen on ") + (tcp ? "tcp" : "udp") + " port " + std::to_string(port));
                    }
                    knServer_setUserPtr(m_server, this);
                    knServer_setOnConnect(m_server, &onConnect);
                    knServer_setOnRead(m_server, &onRead);
                    knServer_setOnDisconnect(m_server, &onDisconnect);
                }

                ~SocketServer(void) override
                {
                    std::lock_guard lock(g_kronknet);

                    m_destroying = true;
                    knServer_destroy(m_server);
                }

                void poll(std::vector<TransportEvent>& out) override
                {
                    {
                        std::lock_guard lock(g_kronknet);

                        // One call of kronknet reads one datagram (or one chunk of a stream): go on while it finds something,
                        // or a busy peer would fill the socket faster than it is emptied
                        for (int round = 0; round < MAX_READS_PER_POLL; ++round) {
                            const std::size_t before = m_events.size();

                            knServer_runOnce(m_server, 0);
                            if (m_events.size() == before) {
                                break;
                            }
                        }
                    }
                    for (auto& event : m_events) {
                        out.push_back(std::move(event));
                    }
                    m_events.clear();
                    // A peer that we closed and that kronknet let go of can be forgotten
                    for (auto it = m_peers.begin(); it != m_peers.end();) {
                        it = it->second->forgotten ? m_peers.erase(it) : std::next(it);
                    }
                }

                bool send(ConnectionId connection, std::span<const std::uint8_t> packet) override
                {
                    const auto found = m_peers.find(connection);

                    if (found == m_peers.end() || found->second->closed || packet.empty() || packet.size() > MAX_PACKET) {
                        return false;
                    }
                    Peer& peer = *found->second;
                    const auto framed = m_tcp ? StreamFramer::frame(packet) : std::vector<std::uint8_t>();
                    int status;

                    {
                        std::lock_guard lock(g_kronknet);
                        status = m_tcp ? knConnection_send(peer.conn, framed.data(), framed.size())
                                       : knConnection_send(peer.conn, packet.data(), packet.size());
                    }
                    if (status != KNEVTOK) {
                        // The buffers are full: this peer does not follow. It is not kept.
                        closePeer(peer, true);
                        return false;
                    }
                    return true;
                }

                void close(ConnectionId connection) override
                {
                    const auto found = m_peers.find(connection);

                    if (found != m_peers.end() && !found->second->closed) {
                        closePeer(*found->second, false);
                    }
                }

                bool reliable(void) const noexcept override { return m_tcp; }
                std::string describe(void) const override { return std::string(m_tcp ? "tcp" : "udp") + " :" + std::to_string(m_port) + " (server)"; }

            private:
                struct Peer
                {
                    SocketServer*  owner;
                    ConnectionId   id;
                    knConnection*  conn;
                    StreamFramer   framer;
                    bool           closed = false;      //!< We ended it (or it ended): no more sending
                    bool           forgotten = false;   //!< kronknet let go of the connection: it can be freed
                };

                // The callbacks run inside knServer_runOnce (under g_kronknet), on the polling thread
                static SocketServer* self(knServer* server) { return static_cast<SocketServer*>(knServer_getUserPtr(server)); }

                static int onConnect(knServer* server, knConnection* conn)
                {
                    self(server)->accept(conn);
                    return KNEVTOK;
                }

                static int onRead(knConnection* conn, const void* data, size_t size)
                {
                    auto* peer = static_cast<Peer*>(knConnection_getUserPtr(conn));

                    if (peer) {
                        peer->owner->read(*peer, conn, std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), size));
                    }
                    return KNEVTOK;
                }

                static int onDisconnect(knServer* server, knConnection* conn)
                {
                    SocketServer* owner = self(server);
                    auto* peer = static_cast<Peer*>(knConnection_getUserPtr(conn));

                    if (owner->m_destroying || !peer) {
                        return KNEVTOK;
                    }
                    if (!peer->closed) {
                        peer->closed = true;
                        owner->m_events.push_back(TransportEvent{TransportEvent::Kind::Disconnected, peer->id, {}, {}});
                    }
                    peer->forgotten = true;
                    knConnection_setUserPtr(conn, nullptr);
                    return KNEVTOK;
                }

                void accept(knConnection* conn)
                {
                    auto peer = std::make_unique<Peer>();

                    peer->owner = this;
                    peer->id = ++m_lastId;
                    peer->conn = conn;
                    knConnection_setUserPtr(conn, peer.get());
                    m_events.push_back(TransportEvent{TransportEvent::Kind::Connected, peer->id, {}, peerName(conn)});
                    m_peers[peer->id] = std::move(peer);
                }

                void read(Peer& peer, knConnection* conn, std::span<const std::uint8_t> bytes)
                {
                    if (peer.closed) {
                        if (m_tcp) {
                            return;   // (about to be let go of)
                        }
                        // UDP: kronknet keeps a connection per address for a while after we closed it, and the
                        // same address may come back with a new endpoint: it is a new peer
                        peer.forgotten = true;
                        accept(conn);
                        read(*static_cast<Peer*>(knConnection_getUserPtr(conn)), conn, bytes);
                        return;
                    }
                    if (!m_tcp) {
                        if (!bytes.empty()) {
                            m_events.push_back(TransportEvent{TransportEvent::Kind::Packet, peer.id, std::vector<std::uint8_t>(bytes.begin(), bytes.end()), {}});
                        }
                        return;
                    }
                    peer.framer.push(bytes);
                    try {
                        std::vector<std::uint8_t> packet;

                        while (peer.framer.next(packet)) {
                            m_events.push_back(TransportEvent{TransportEvent::Kind::Packet, peer.id, std::move(packet), {}});
                            packet.clear();
                        }
                    } catch (const std::length_error& error) {
                        Logger::logger().warn("net: {} sent nonsense ({}): disconnected", describe(), error.what());
                        closePeer(peer, true);
                    }
                }

                // Under g_kronknet (called from callbacks) or not: it only marks
                void closePeer(Peer& peer, bool report)
                {
                    peer.closed = true;
                    if (report) {
                        m_events.push_back(TransportEvent{TransportEvent::Kind::Disconnected, peer.id, {}, {}});
                    }
                    knConnection_disconnect(peer.conn);   // kronknet ends it when it next polls (TCP)
                }

                bool                                    m_tcp;
                std::uint16_t                           m_port;
                knServer*                               m_server = nullptr;
                bool                                    m_destroying = false;
                ConnectionId                            m_lastId = 0;
                std::map<ConnectionId, std::unique_ptr<Peer>> m_peers;
                std::vector<TransportEvent>             m_events;
        };

        // -- Client ---------------------------------------------------------------------------
        class SocketClient final : public ITransport
        {
            public:
                SocketClient(std::string host, std::uint16_t port, bool tcp) : m_tcp(tcp), m_host(std::move(host)), m_port(port)
                {
                    std::lock_guard lock(g_kronknet);

                    m_client = knClient_create(tcp ? knTCP : knUDP);
                    if (!m_client) {
                        throw std::runtime_error("net: cannot make a socket");
                    }
                    knClient_setUserPtr(m_client, this);
                    knClient_setOnConnect(m_client, &onConnect);
                    knClient_setOnRead(m_client, &onRead);
                    knClient_setOnDisconnect(m_client, &onDisconnect);
                    if (knClient_connect(m_client, m_host.c_str(), port) != KNEVTOK) {
                        m_events.push_back(TransportEvent{TransportEvent::Kind::Disconnected, CLIENT_CONNECTION, {}, {}});
                        m_over = true;   // (nothing to poll)
                    }
                }

                ~SocketClient(void) override { release(); }

                void poll(std::vector<TransportEvent>& out) override
                {
                    if (m_client && !m_over) {
                        {
                            std::lock_guard lock(g_kronknet);

                            for (int round = 0; round < MAX_READS_PER_POLL; ++round) {
                                const std::size_t before = m_events.size();

                                if (knClient_runOnce(m_client, 0) != KNEVTOK || m_events.size() == before) {
                                    break;
                                }
                            }
                            // kronknet reports an end of stream by stopping, without always calling back
                            if (!m_over && !knClient_isRunning(m_client)) {
                                lost();
                            }
                        }
                    }
                    for (auto& event : m_events) {
                        out.push_back(std::move(event));
                    }
                    m_events.clear();
                }

                bool send(ConnectionId connection, std::span<const std::uint8_t> packet) override
                {
                    if (connection != CLIENT_CONNECTION || m_over || !m_client || packet.empty() || packet.size() > MAX_PACKET) {
                        return false;
                    }
                    const auto framed = m_tcp ? StreamFramer::frame(packet) : std::vector<std::uint8_t>();
                    int status;

                    {
                        std::lock_guard lock(g_kronknet);
                        status = m_tcp ? knClient_sendServer(m_client, framed.data(), framed.size())
                                       : knClient_sendServer(m_client, packet.data(), packet.size());
                    }
                    if (status != KNEVTOK) {
                        std::lock_guard lock(g_kronknet);

                        lost();
                        return false;
                    }
                    return true;
                }

                void close(ConnectionId) override
                {
                    m_over = true;
                    release();
                }

                bool reliable(void) const noexcept override { return m_tcp; }
                std::string describe(void) const override { return std::string(m_tcp ? "tcp " : "udp ") + m_host + ":" + std::to_string(m_port) + " (client)"; }

            private:
                static SocketClient* self(knClient* client) { return static_cast<SocketClient*>(knClient_getUserPtr(client)); }

                static int onConnect(knClient* client)
                {
                    SocketClient* owner = self(client);

                    owner->m_events.push_back(TransportEvent{TransportEvent::Kind::Connected, CLIENT_CONNECTION, {}, owner->m_host + ":" + std::to_string(owner->m_port)});
                    return KNEVTOK;
                }

                static int onRead(knClient* client, const void* data, size_t size)
                {
                    self(client)->read(std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), size));
                    return KNEVTOK;
                }

                static int onDisconnect(knClient* client)
                {
                    self(client)->lost();
                    return KNEVTOK;
                }

                void read(std::span<const std::uint8_t> bytes)
                {
                    if (m_over) {
                        return;
                    }
                    if (!m_tcp) {
                        if (!bytes.empty()) {
                            m_events.push_back(TransportEvent{TransportEvent::Kind::Packet, CLIENT_CONNECTION, std::vector<std::uint8_t>(bytes.begin(), bytes.end()), {}});
                        }
                        return;
                    }
                    m_framer.push(bytes);
                    try {
                        std::vector<std::uint8_t> packet;

                        while (m_framer.next(packet)) {
                            m_events.push_back(TransportEvent{TransportEvent::Kind::Packet, CLIENT_CONNECTION, std::move(packet), {}});
                            packet.clear();
                        }
                    } catch (const std::length_error& error) {
                        Logger::logger().warn("net: {} received nonsense ({}): disconnected", describe(), error.what());
                        lost();
                    }
                }

                // The connection is over (told once)
                void lost(void)
                {
                    if (!m_over) {
                        m_over = true;
                        m_events.push_back(TransportEvent{TransportEvent::Kind::Disconnected, CLIENT_CONNECTION, {}, {}});
                    }
                }

                void release(void)
                {
                    std::lock_guard lock(g_kronknet);

                    if (m_client) {
                        knClient_destroy(m_client);
                        m_client = nullptr;
                    }
                }

                bool                         m_tcp;
                std::string                  m_host;
                std::uint16_t                m_port;
                knClient*                    m_client = nullptr;
                bool                         m_over = false;
                StreamFramer                 m_framer;
                std::vector<TransportEvent>  m_events;
        };
    }

    std::unique_ptr<ITransport> makeTcpServer(std::uint16_t port) { return std::make_unique<SocketServer>(port, true); }
    std::unique_ptr<ITransport> makeUdpServer(std::uint16_t port) { return std::make_unique<SocketServer>(port, false); }
    std::unique_ptr<ITransport> makeTcpClient(const std::string& host, std::uint16_t port) { return std::make_unique<SocketClient>(host, port, true); }
    std::unique_ptr<ITransport> makeUdpClient(const std::string& host, std::uint16_t port) { return std::make_unique<SocketClient>(host, port, false); }

}
