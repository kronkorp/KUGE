#include "NetMeter.hpp"
#include <algorithm>

const kuge::NetReport& kuge::net::NetMeter::update(const Endpoint& endpoint, std::optional<ConnectionId> connection, double now,
    std::uint64_t entities)
{
    const EndpointStats stats = endpoint.stats();

    if (connection) {
        if (const auto rtt = endpoint.rtt(*connection)) {
            m_report.rttSeconds = *rtt;
            m_report.rttLow = m_report.rttLow < 0.0 ? *rtt : std::min(m_report.rttLow, *rtt);
            m_report.rttHigh = std::max(m_report.rttHigh, *rtt);
        }
    }
    m_report.resends = stats.resends;
    m_report.duplicates = stats.duplicates;
    m_report.malformed = stats.malformed;
    m_report.entities = entities;
    if (m_at < 0.0) {
        m_at = now;
        m_before = stats;
    } else if (now - m_at >= m_interval) {
        const double seconds = now - m_at;

        m_report.sentPerSecond = static_cast<double>(stats.bytesSent - m_before.bytesSent) / seconds;
        m_report.receivedPerSecond = static_cast<double>(stats.bytesReceived - m_before.bytesReceived) / seconds;
        m_report.packetsSentPerSecond = static_cast<double>(stats.packetsSent - m_before.packetsSent) / seconds;
        m_report.packetsReceivedPerSecond = static_cast<double>(stats.packetsReceived - m_before.packetsReceived) / seconds;
        m_at = now;
        m_before = stats;
    }
    return m_report;
}
