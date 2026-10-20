#pragma once

#include "Loopback.hpp"
#include "Transport.hpp"
#include <deque>
#include <functional>
#include <memory>
#include <random>

namespace kuge::net
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Makes any transport behave like a bad network: packets lost, repeated, late, out of order
     *
     * It wraps a transport (UDP, TCP, the loopback) and applies the Conditions of the LoopbackNetwork to **each packet, in each
     * direction**: `latency` is therefore paid twice by a round trip. The same seed gives the same losses. A client on the real
     * socket of a real machine can so be tried against a bad network without clumsy or netem, on Linux and on Windows.
     *
     *     kuge::net::EndpointConfig config;
     *     config.simulate = kuge::net::LoopbackNetwork::Conditions{.loss = 0.02, .duplicate = 0.02, .latency = 0.075, .seed = 7};
     *     auto& client = net.connect(kuge::net::Protocol::Udp, "127.0.0.1", 4242, config);
     *     client.setSimulation(kuge::net::LoopbackNetwork::Conditions{.latency = 0.2});   // changed while playing
     *
     * A transport that is `reliable()` (TCP) is not allowed to lose, repeat or reorder: only `latency` applies to it (in order), or
     * the endpoint, which does not send again over it, would lose data for good.
     */
    ////////////////////////////////////////////////////////////////////////////
    class SimulatedTransport final : public ITransport
    {
        public:
            using Conditions = LoopbackNetwork::Conditions;

            //! @param clock  Seconds (steady clock if not given)
            SimulatedTransport(std::unique_ptr<ITransport> inner, Conditions conditions, std::function<double(void)> clock = {});

            void setConditions(const Conditions& conditions);
            const Conditions& conditions(void) const noexcept { return m_conditions; }

            //! How many packets the simulation dropped (both directions)
            std::uint64_t dropped(void) const noexcept { return m_dropped; }

            // ITransport
            void poll(std::vector<TransportEvent>& out) override;
            bool send(ConnectionId connection, std::span<const std::uint8_t> packet) override;
            void close(ConnectionId connection) override { m_inner->close(connection); }
            bool reliable(void) const noexcept override { return m_inner->reliable(); }
            std::string describe(void) const override { return "simulated " + m_inner->describe(); }

        private:
            struct Late
            {
                double                     at;
                std::uint64_t              order;
                ConnectionId               connection;
                std::vector<std::uint8_t>  packet;
            };

            // Puts a packet in a queue as the conditions say (nothing, or one or two copies, later)
            void admit(std::deque<Late>& queue, ConnectionId connection, std::span<const std::uint8_t> packet);

            std::unique_ptr<ITransport>  m_inner;
            Conditions                   m_conditions;
            std::function<double(void)>  m_clock;
            std::mt19937_64              m_rng;
            std::deque<Late>             m_outgoing;
            std::deque<Late>             m_incoming;
            std::uint64_t                m_order = 0;
            std::uint64_t                m_dropped = 0;
    };

}
