#pragma once

#include "Endpoint.hpp"
#include "Stats.hpp"
#include <optional>

namespace kuge::net
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Turns the counters of an Endpoint into the numbers a person reads: a round trip with its low and high, bytes and packets a
     *         second, resends, duplicates and bad packets (a NetReport)
     *
     *     kuge::net::NetMeter meter;
     *     ...   // each frame, with the endpoint of the room
     *     const kuge::NetReport& now = meter.update(endpoint, kuge::net::CLIENT_CONNECTION, time, replicatedEntities);
     *     log.info("{}", kuge::describe(now));
     *
     * The rates are worked out over `interval` seconds (the report keeps its last values between two of them). The server has no round trip
     * to report for a whole endpoint: pass no connection.
     */
    ////////////////////////////////////////////////////////////////////////////
    class NetMeter
    {
        public:
            explicit NetMeter(double interval = 1.0) : m_interval(interval) {}

            //! @param now  Seconds, from a clock that never goes back
            const NetReport& update(const Endpoint& endpoint, std::optional<ConnectionId> connection, double now, std::uint64_t entities = 0);

            const NetReport& report(void) const noexcept { return m_report; }

        private:
            double        m_interval;
            double        m_at = -1.0;
            EndpointStats m_before;
            NetReport     m_report;
    };

}
