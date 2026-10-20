#include "SimulatedTransport.hpp"
#include <algorithm>
#include <chrono>

namespace
{
    double steadySeconds(void)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

kuge::net::SimulatedTransport::SimulatedTransport(std::unique_ptr<ITransport> inner, Conditions conditions, std::function<double(void)> clock)
    : m_inner(std::move(inner)), m_conditions(conditions), m_clock(clock ? std::move(clock) : std::function<double(void)>(steadySeconds)),
      m_rng(conditions.seed)
{
}

void kuge::net::SimulatedTransport::setConditions(const Conditions& conditions)
{
    m_conditions = conditions;
}

void kuge::net::SimulatedTransport::admit(std::deque<Late>& queue, ConnectionId connection, std::span<const std::uint8_t> packet)
{
    const auto random = [this] { return std::uniform_real_distribution<double>(0.0, 1.0)(m_rng); };
    const bool safe = m_inner->reliable();   // TCP: late, but never lost, repeated or reordered

    if (m_conditions.mtu != 0 && packet.size() > m_conditions.mtu && !safe) {
        ++m_dropped;
        return;
    }
    if (!safe && random() < m_conditions.loss) {
        ++m_dropped;
        return;
    }
    const int copies = !safe && random() < m_conditions.duplicate ? 2 : 1;

    for (int i = 0; i < copies; ++i) {
        queue.push_back({m_clock() + m_conditions.latency + (safe ? 0.0 : m_conditions.jitter * random()), m_order++, connection,
            std::vector<std::uint8_t>(packet.begin(), packet.end())});
    }
}

bool kuge::net::SimulatedTransport::send(ConnectionId connection, std::span<const std::uint8_t> packet)
{
    if (m_conditions.latency <= 0.0 && m_conditions.jitter <= 0.0 && m_outgoing.empty()) {
        // No delay to simulate: loss and repeats still apply, and the packet goes now
        const std::size_t before = m_outgoing.size();

        admit(m_outgoing, connection, packet);
        if (m_outgoing.size() == before) {
            return true;   // lost on its way: the sender cannot know
        }
        bool sent = true;

        while (!m_outgoing.empty()) {
            sent = m_inner->send(m_outgoing.front().connection, m_outgoing.front().packet) && sent;
            m_outgoing.pop_front();
        }
        return sent;
    }
    admit(m_outgoing, connection, packet);
    return true;
}

void kuge::net::SimulatedTransport::poll(std::vector<TransportEvent>& out)
{
    const double now = m_clock();

    // Outgoing packets that are due, by time
    std::stable_sort(m_outgoing.begin(), m_outgoing.end(), [](const Late& a, const Late& b) { return a.at != b.at ? a.at < b.at : a.order < b.order; });
    while (!m_outgoing.empty() && m_outgoing.front().at <= now) {
        m_inner->send(m_outgoing.front().connection, m_outgoing.front().packet);
        m_outgoing.pop_front();
    }
    // What the inner transport brought: packets go through the simulation, the rest as it is
    std::vector<TransportEvent> fresh;

    m_inner->poll(fresh);
    for (TransportEvent& event : fresh) {
        if (event.kind == TransportEvent::Kind::Packet) {
            admit(m_incoming, event.connection, event.packet);
        } else {
            out.push_back(std::move(event));
        }
    }
    std::stable_sort(m_incoming.begin(), m_incoming.end(), [](const Late& a, const Late& b) { return a.at != b.at ? a.at < b.at : a.order < b.order; });
    while (!m_incoming.empty() && m_incoming.front().at <= now) {
        out.push_back({TransportEvent::Kind::Packet, m_incoming.front().connection, std::move(m_incoming.front().packet), {}});
        m_incoming.pop_front();
    }
}
