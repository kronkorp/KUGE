#pragma once

#include "Transport.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace kuge::net
{

    //! The real network, through kronknet. IPv4 only: a host is "localhost" or a dotted address.
    //! Every one of these throws std::runtime_error if the socket cannot be made (a server:
    //! port taken; a client: the address is not one). A client that cannot reach the server
    //! does not throw: its transport says Disconnected at its first poll.
    //!
    //! TCP: the stream is cut into whole packets (each is sent with its length, and put back
    //! together on the other side), so a transport gives the same packets as a datagram one.
    //! A peer that sends a length larger than MAX_PACKET is disconnected.
    //! UDP: a packet is a datagram.

    std::unique_ptr<ITransport> makeTcpServer(std::uint16_t port);
    std::unique_ptr<ITransport> makeTcpClient(const std::string& host, std::uint16_t port);
    std::unique_ptr<ITransport> makeUdpServer(std::uint16_t port);
    std::unique_ptr<ITransport> makeUdpClient(const std::string& host, std::uint16_t port);

    //! Cuts a stream into packets. TCP gives bytes in pieces that do not respect what was sent:
    //! this puts them back together. Public because it is worth testing on its own.
    class StreamFramer
    {
        public:
            //! What a packet looks like on a stream: 4 bytes of length (little-endian), then the packet
            static std::vector<std::uint8_t> frame(std::span<const std::uint8_t> packet);

            //! Adds what arrived
            void push(std::span<const std::uint8_t> bytes);

            //! The next whole packet, if there is one
            //! @return  false if it is not all there yet
            //! @throw   std::length_error if the length announced is 0 or more than MAX_PACKET (the stream is not to be trusted any more)
            bool next(std::vector<std::uint8_t>& packet);

            //! Bytes kept for a packet that is not complete
            std::size_t pending(void) const noexcept { return m_buffer.size() - m_start; }

        private:
            std::vector<std::uint8_t> m_buffer;
            std::size_t               m_start = 0;
    };

}
