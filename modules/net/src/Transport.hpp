#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kuge::net
{

    //! One remote peer of a transport. On the client side, the server is always CLIENT_CONNECTION.
    using ConnectionId = std::uint32_t;
    constexpr ConnectionId CLIENT_CONNECTION = 1;

    //! The largest packet a transport carries (what kronknet reads at once)
    constexpr std::size_t MAX_PACKET = 8192;

    //! What happened on a transport since the last poll()
    struct TransportEvent
    {
        enum class Kind : std::uint8_t {
            Connected,      //!< A peer is there (a server: someone reached it; a client: the socket is made)
            Disconnected,   //!< The peer is gone, or could not be reached
            Packet,         //!< A whole packet arrived
        };

        Kind                       kind;
        ConnectionId               connection;
        std::vector<std::uint8_t>  packet;   //!< Kind::Packet
        std::string                peer;     //!< Kind::Connected: where it comes from, for humans
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What carries packets between two machines (or two threads): the
     *         same interface for TCP, UDP and the in-memory loopback
     *
     * A transport moves **whole packets** of at most MAX_PACKET bytes. It says
     * nothing about what is in them (see Endpoint). What it promises depends on
     * the protocol: a TCP transport is reliable and ordered (reliable() is
     * true), a UDP one can lose, repeat and reorder packets.
     *
     * It never blocks, and is used by one thread: the one that polls it.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ITransport
    {
        public:
            virtual ~ITransport(void) = default;

            //! Reads what happened, without waiting. The events are added to out.
            virtual void poll(std::vector<TransportEvent>& out) = 0;

            //! Sends one packet
            //! @return  false if it was refused: too big, unknown connection, closed, or the buffers are full
            virtual bool send(ConnectionId connection, std::span<const std::uint8_t> packet) = 0;

            //! Ends a connection (the peer sees it go)
            virtual void close(ConnectionId connection) = 0;

            //! Are packets delivered, once, in order? (No need to send them again.)
            virtual bool reliable(void) const noexcept = 0;

            //! For logs: "tcp :4242", "loopback 'room'"...
            virtual std::string describe(void) const = 0;
    };

}
