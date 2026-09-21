#include "Reliable.hpp"
#include <algorithm>
#include <cmath>

bool kuge::net::ReliableChannel::queue(std::vector<std::uint8_t> payload)
{
    if (m_queued.size() >= m_config.maxQueued) {
        return false;
    }
    m_queued.push_back(std::move(payload));
    return true;
}

double kuge::net::ReliableChannel::rto(void) const noexcept
{
    if (!m_hasRtt) {
        return m_config.initialRto;
    }
    return std::clamp(m_srtt + 4.0 * m_rttvar, m_config.minRto, m_config.maxRto);
}

void kuge::net::ReliableChannel::collect(double now, std::vector<Outgoing>& out)
{
    while (!m_queued.empty() && m_inflight.size() < m_config.window) {
        Sent sent;

        sent.seq = m_nextSeq++;
        sent.payload = std::move(m_queued.front());
        m_queued.pop_front();
        sent.firstSent = sent.lastSent = now;
        sent.sends = 1;
        m_inflight.push_back(std::move(sent));
        out.push_back(Outgoing{m_inflight.back().seq, &m_inflight.back().payload, false});
    }
    if (!m_config.resend) {
        return;
    }
    for (Sent& sent : m_inflight) {
        if (sent.sends == 1 && sent.lastSent == now) {
            continue;   // just sent, above
        }
        // Each miss doubles the wait (up to the limit)
        const double wait = std::min(rto() * std::pow(2.0, std::min<std::uint32_t>(sent.sends - 1, 8)), m_config.maxRto);
        // A message that later ones have overtaken is very likely lost: no need to wait for the whole wait
        const bool overtaken = m_anyAcked && sent.seq < m_highestAcked && sent.sends == 1
            && now - sent.lastSent >= std::max(m_config.minRto, m_hasRtt ? m_srtt * 1.5 : m_config.minRto);

        if (now - sent.lastSent >= wait || overtaken) {
            sent.lastSent = now;
            ++sent.sends;
            ++m_resends;
            out.push_back(Outgoing{sent.seq, &sent.payload, true});
        }
    }
}

void kuge::net::ReliableChannel::acknowledge(std::uint32_t seq, double now)
{
    const auto found = std::find_if(m_inflight.begin(), m_inflight.end(), [seq](const Sent& sent) { return sent.seq == seq; });

    if (found == m_inflight.end()) {
        return;
    }
    // Only a message that was sent once shows the round trip (an ack could be for any of the copies)
    if (found->sends == 1) {
        const double sample = std::max(0.0, now - found->firstSent);

        if (!m_hasRtt) {
            m_srtt = sample;
            m_rttvar = sample / 2.0;
            m_hasRtt = true;
        } else {
            m_rttvar = 0.75 * m_rttvar + 0.25 * std::fabs(m_srtt - sample);
            m_srtt = 0.875 * m_srtt + 0.125 * sample;
        }
    }
    if (!m_anyAcked || seq > m_highestAcked) {
        m_highestAcked = seq;
        m_anyAcked = true;
    }
    m_inflight.erase(found);
}

void kuge::net::ReliableChannel::onAck(std::uint32_t ack, std::uint32_t bits, double now)
{
    // Everything below ack, then what the bits say beyond it
    while (!m_inflight.empty() && m_inflight.front().seq < ack) {
        acknowledge(m_inflight.front().seq, now);
    }
    for (std::uint32_t i = 0; i < 32; ++i) {
        if (bits & (1u << i)) {
            acknowledge(ack + 1 + i, now);
        }
    }
}

void kuge::net::ReliableChannel::onData(std::uint32_t seq, std::vector<std::uint8_t> payload)
{
    // Whatever arrives is worth an ack: even a copy (the first ack may have been lost)
    m_ackPending = true;
    if (seq < m_next) {
        ++m_duplicates;
        return;
    }
    if (seq >= m_next + m_config.window + 32) {
        return;   // too far ahead to be honest: not kept, not acknowledged
    }
    if (seq != m_next) {
        if (!m_stash.emplace(seq, std::move(payload)).second) {
            ++m_duplicates;
        }
        return;
    }
    m_delivered.push_back(std::move(payload));
    ++m_next;
    for (auto it = m_stash.begin(); it != m_stash.end() && it->first == m_next; it = m_stash.erase(it)) {
        m_delivered.push_back(std::move(it->second));
        ++m_next;
    }
}

std::vector<std::vector<std::uint8_t>> kuge::net::ReliableChannel::takeDelivered(void)
{
    std::vector<std::vector<std::uint8_t>> taken;

    taken.swap(m_delivered);
    return taken;
}

std::uint32_t kuge::net::ReliableChannel::ackBits(void) const noexcept
{
    std::uint32_t bits = 0;

    for (const auto& [seq, payload] : m_stash) {
        const std::uint32_t offset = seq - m_next;   // (at least 1: m_next itself is not there)

        if (offset >= 1 && offset <= 32) {
            bits |= 1u << (offset - 1);
        }
    }
    return bits;
}
