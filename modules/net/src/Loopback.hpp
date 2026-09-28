#pragma once

#include "Transport.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace kuge::net
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A network inside the process: transports that reach each other by
     *         name, with no socket. For tests, and for a player who hosts the
     *         match (a client and a server in the same process).
     *
     *     kuge::net::LoopbackNetwork network;
     *     auto server = network.listen("room");
     *     auto client = network.connect("room");
     *
     * It is thread-safe: the two sides can be polled by different threads.
     * It can also behave badly on purpose (Conditions): packets lost, repeated,
     * late and out of order, to test what is built on it. Time (for latency) is
     * the network's clock, so a test can make its own.
     */
    ////////////////////////////////////////////////////////////////////////////
    class LoopbackNetwork
    {
        public:
            struct Conditions
            {
                double        loss      = 0.0;   //!< Chance (0 to 1) that a packet is lost
                double        duplicate = 0.0;   //!< Chance that a packet arrives twice
                double        latency   = 0.0;   //!< Seconds a packet takes
                double        jitter    = 0.0;   //!< Up to this much more, at random (so packets can pass each other)
                std::uint64_t seed      = 1;     //!< Same seed, same losses
            };

            //! A network that behaves
            LoopbackNetwork(void);

            //! @param clock  Seconds (steady clock if not given)
            explicit LoopbackNetwork(Conditions conditions, std::function<double(void)> clock = {});
            ~LoopbackNetwork(void);

            LoopbackNetwork(const LoopbackNetwork&)            = delete;
            LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;

            //! The network that transports use when nobody gives theirs
            static LoopbackNetwork& global(void);

            //! Starts listening at a name
            //! @throw std::runtime_error if the name is taken
            std::unique_ptr<ITransport> listen(const std::string& name);

            //! Connects to a name. If nobody listens, the transport says Disconnected at its first poll.
            std::unique_ptr<ITransport> connect(const std::string& name);

            //! Changes how the network behaves, from now on
            void setConditions(const Conditions& conditions);

            std::uint64_t packetsSent(void) const;
            std::uint64_t packetsLost(void) const;

            struct Impl;   //!< (internal)

        private:
            std::shared_ptr<Impl> m_impl;   // (shared: the transports outlive nothing, but keep it while they exist)
    };

}
