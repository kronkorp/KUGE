#pragma once

#include "Reliable.hpp"
#include "Transport.hpp"
#include "Wire.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace kuge::net
{

    enum class Role : std::uint8_t { Server, Client };

    //! What a message needs
    enum class Channel : std::uint8_t {
        Unreliable = 0,   //!< Sent once: may be lost, may arrive twice, in any order (positions, inputs)
        Reliable   = 1,   //!< Arrives once and in order, however many times it takes (chat, events, handshakes)
    };

    enum class DisconnectReason : std::uint8_t {
        Local,         //!< We ended it (disconnect())
        Remote,        //!< The other side ended it
        Timeout,       //!< Nothing was heard from it for too long (or it never answered)
        Refused,       //!< The server said no (full, or another version of the protocol)
        Unreachable,   //!< The transport could not reach it, or lost it
    };

    struct EndpointConfig
    {
        std::function<double(void)>  clock;                  //!< Seconds; the steady clock if not given
        double                       timeout        = 10.0;  //!< A peer that says nothing for this long is gone
        double                       keepAlive      = 1.0;   //!< A silent connection sends something this often
        double                       connectRetry   = 0.25;  //!< A client asks again this often
        double                       connectTimeout = 5.0;   //!< ... and gives up after this
        std::size_t                  maxConnections = 64;    //!< Server: the next ones are refused
        std::uint16_t                protocol       = 1;     //!< Version of what is said: both sides must have the same
        ReliableChannel::Config      reliable;
    };

    struct EndpointStats
    {
        std::uint64_t packetsSent = 0, packetsReceived = 0;
        std::uint64_t bytesSent = 0, bytesReceived = 0;
        std::uint64_t messagesSent = 0, messagesReceived = 0;
        std::uint64_t resends = 0;
        std::uint64_t duplicates = 0;        //!< Reliable messages received again
        std::uint64_t malformed = 0;         //!< Packets or messages that made no sense (dropped)
        std::uint64_t unknownMessages = 0;   //!< Messages nobody listens to (dropped)
        std::uint64_t refusedSends = 0;      //!< Messages that could not be sent (too big, full, not connected)
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  One end of a network: a server that accepts connections, or a
     *         client that makes one. It sends and receives typed messages
     *         (KUGE_MESSAGE) on channels, and tells when peers come and go.
     *
     *     auto server = kuge::net::Endpoint(network.listen("room"), kuge::net::Role::Server);
     *     server.on<Chat>([&](kuge::net::ConnectionId from, const Chat& chat) {
     *         server.broadcast(chat, kuge::net::Channel::Reliable);
     *     });
     *
     *     while (running) { server.poll(); ... }         // once per tick (the Network stage)
     *
     * It does everything on top of a transport, whatever its protocol: a small
     * handshake (so "connected" means the other side answered, even over UDP),
     * reliable messages (numbered, acknowledged, sent again, delivered in order),
     * keep-alive and timeouts, and the dispatch of messages to handlers.
     *
     * **Thread**: an endpoint belongs to one thread, the one that calls poll().
     * Handlers run inside poll() (and inside disconnect()), after the endpoint has
     * done its own work, so they may send, broadcast and disconnect freely. They may
     * even destroy the endpoint (or remove it from its Net): nothing of it runs after.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Endpoint
    {
        public:
            using ConnectedHandler    = std::function<void(ConnectionId)>;
            using DisconnectedHandler = std::function<void(ConnectionId, DisconnectReason)>;

            //! A client starts to connect at once (it is Connecting until the server answers)
            Endpoint(std::unique_ptr<ITransport> transport, Role role);
            Endpoint(std::unique_ptr<ITransport> transport, Role role, EndpointConfig config);
            ~Endpoint(void);

            Endpoint(const Endpoint&)            = delete;
            Endpoint& operator=(const Endpoint&) = delete;

            //! Reads what came, sends what is due (acks, resends, keep-alives), then calls the handlers
            void poll(void);

            // -- Sending --------------------------------------------------------------------
            //! @return  false if it could not be sent: not connected, too big for a packet, or too many wait
            template<NetMessage T>
            bool send(ConnectionId to, const T& message, Channel channel = Channel::Reliable)
            {
                ByteWriter out;

                out.write<std::uint32_t>(T::kugeMessageId);
                encode(out, message);
                return sendPayload(to, out.bytes(), channel);
            }

            //! To every connected peer (but one). @return  How many it was sent to
            template<NetMessage T>
            std::size_t broadcast(const T& message, Channel channel = Channel::Reliable, std::optional<ConnectionId> except = {})
            {
                std::size_t count = 0;

                for (const ConnectionId id : connections()) {
                    if (id != except && send(id, message, channel)) {
                        ++count;
                    }
                }
                return count;
            }

            //! Ends a connection: the peer is told, then it is gone (a Local disconnection is reported)
            void disconnect(ConnectionId connection);

            // -- Receiving ------------------------------------------------------------------
            //! Says what to do with a message. Replaces the handler of that type.
            template<NetMessage T>
            void on(std::function<void(ConnectionId, const T&)> handler)
            {
                registerHandler(T::kugeMessageId, T::kugeMessageName, [handler = std::move(handler)](ConnectionId from, ByteReader& in) {
                    T message;

                    decode(in, message);
                    if (!in.atEnd()) {
                        throw SerializerError("a message longer than its fields");
                    }
                    handler(from, message);
                });
            }

            void onConnected(ConnectedHandler handler) { m_onConnected = std::move(handler); }
            void onDisconnected(DisconnectedHandler handler) { m_onDisconnected = std::move(handler); }

            // -- State ----------------------------------------------------------------------
            Role role(void) const noexcept { return m_role; }

            //! Client: is the connection made? Server: is anyone connected?
            bool connected(void) const;

            //! Client only: the connection failed or ended (and no other will be made)
            bool closed(void) const noexcept { return m_role == Role::Client && m_clientClosed; }

            //! The peers that are connected (a client has at most one: the server)
            std::vector<ConnectionId> connections(void) const;

            //! Seconds a packet takes to go and come back, as the acks show (nothing before the first)
            std::optional<double> rtt(ConnectionId connection) const;

            std::string           peer(ConnectionId connection) const;
            EndpointStats         stats(void) const;
            ITransport&           transport(void) noexcept { return *m_transport; }

        private:
            enum class State : std::uint8_t { Connecting, Handshaking, Connected };

            struct Connection
            {
                ConnectionId     id;
                State            state;
                double           created = 0.0, lastReceived = 0.0, lastSent = 0.0, nextConnect = 0.0;
                std::uint32_t    salt = 0;
                bool             transportUp = false;
                ReliableChannel  reliable;
                std::string      peer;
                bool             refusing = false;   //!< Server: it came when there was no room. It gets its Reject, and goes.
            };

            struct Handler
            {
                std::string                                                     name;
                // (shared: the one that runs is a copy, which lives on if the handler replaces itself)
                std::shared_ptr<const std::function<void(ConnectionId, ByteReader&)>> run;
            };

            double now(void) const { return m_clock(); }
            void   registerHandler(std::uint32_t id, const char* name, std::function<void(ConnectionId, ByteReader&)> run);
            bool   sendPayload(ConnectionId to, const std::vector<std::uint8_t>& payload, Channel channel);

            void handleEvent(TransportEvent& event, double time);
            void handlePacket(Connection& connection, std::span<const std::uint8_t> packet, double time);
            void deferMessage(ConnectionId from, std::span<const std::uint8_t> message);
            void handleMessage(ConnectionId from, std::span<const std::uint8_t> message);
            void service(Connection& connection, double time);
            void flushReliable(Connection& connection, double time);
            void transmit(Connection& connection, const std::vector<std::uint8_t>& packet, double time);
            void sendData(Connection& connection, Channel channel, bool control, std::uint32_t seq, const std::vector<std::uint8_t>* payload, double time);
            void sendSimple(Connection& connection, std::uint8_t type, std::uint32_t value, double time);
            void establish(Connection& connection);
            void close(ConnectionId id, DisconnectReason reason, bool tellPeer, bool closeTransport);
            void runDeferred(void);

            std::unique_ptr<ITransport>                 m_transport;
            Role                                        m_role;
            EndpointConfig                              m_config;
            std::function<double(void)>                 m_clock;
            std::map<ConnectionId, Connection>          m_connections;
            std::unordered_map<std::uint32_t, Handler>  m_handlers;
            ConnectedHandler                            m_onConnected;
            DisconnectedHandler                         m_onDisconnected;
            std::vector<std::function<void(void)>>      m_deferred;
            bool                                        m_inPoll = false;
            bool                                        m_clientClosed = false;
            EndpointStats                               m_stats;
            std::uint64_t                               m_closedResends = 0, m_closedDuplicates = 0;   // of connections that are gone
            std::shared_ptr<bool>                       m_alive = std::make_shared<bool>(true);         // false once destroyed (by a handler, maybe)
    };

}
