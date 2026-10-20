#include "Stats.hpp"
#include <algorithm>
#include <format>

void kuge::FrameStats::push(double frameSeconds, double tickSeconds, double workSeconds)
{
    m_samples.push_back({frameSeconds, tickSeconds, workSeconds});
    if (m_samples.size() > WINDOW) {
        m_samples.pop_front();
    }
    ++m_total;
}

double kuge::FrameStats::frameMean(void) const
{
    if (m_samples.empty()) {
        return 0.0;
    }
    double sum = 0.0;

    for (const Sample& sample : m_samples) {
        sum += sample.frame;
    }
    return sum / static_cast<double>(m_samples.size());
}

double kuge::FrameStats::fps(void) const
{
    const double mean = frameMean();

    return mean > 0.0 ? 1.0 / mean : 0.0;
}

double kuge::FrameStats::frameWorst(void) const
{
    double worst = 0.0;

    for (const Sample& sample : m_samples) {
        worst = std::max(worst, sample.frame);
    }
    return worst;
}

double kuge::FrameStats::frameWorstPercent(void) const
{
    if (m_samples.empty()) {
        return 0.0;
    }
    std::vector<double> frames;

    for (const Sample& sample : m_samples) {
        frames.push_back(sample.frame);
    }
    std::sort(frames.begin(), frames.end(), std::greater<>());
    const std::size_t count = std::max<std::size_t>(1, frames.size() / 100);
    double sum = 0.0;

    for (std::size_t i = 0; i < count; ++i) {
        sum += frames[i];
    }
    return sum / static_cast<double>(count);
}

double kuge::FrameStats::tickMean(void) const
{
    double sum = 0.0;
    std::size_t count = 0;

    for (const Sample& sample : m_samples) {
        if (sample.tick > 0.0) {
            sum += sample.tick;
            ++count;
        }
    }
    return count ? sum / static_cast<double>(count) : 0.0;
}

double kuge::FrameStats::workMean(void) const
{
    if (m_samples.empty()) {
        return 0.0;
    }
    double sum = 0.0;

    for (const Sample& sample : m_samples) {
        sum += sample.work;
    }
    return sum / static_cast<double>(m_samples.size());
}

std::vector<double> kuge::FrameStats::recent(std::size_t count) const
{
    std::vector<double> frames;
    const std::size_t from = m_samples.size() > count ? m_samples.size() - count : 0;

    for (std::size_t i = from; i < m_samples.size(); ++i) {
        frames.push_back(m_samples[i].frame);
    }
    return frames;
}

std::string kuge::describe(const NetReport& r)
{
    const std::string rtt = r.rttSeconds < 0.0 ? "rtt -" : std::format("rtt {:.0f} ms ({:.0f}-{:.0f})", r.rttSeconds * 1000.0, std::max(r.rttLow, 0.0) * 1000.0,
        std::max(r.rttHigh, 0.0) * 1000.0);

    return std::format("{}  down {:.1f} KiB/s  up {:.1f} KiB/s  pkts {:.0f}/{:.0f}  resends {} dup {} bad {}", rtt, r.receivedPerSecond / 1024.0,
        r.sentPerSecond / 1024.0, r.packetsReceivedPerSecond, r.packetsSentPerSecond, r.resends, r.duplicates, r.malformed);
}

kuge::Gauges& kuge::Gauges::global(void)
{
    static Gauges gauges;

    return gauges;
}

void kuge::Gauges::set(const std::string& name, double value)
{
    std::lock_guard lock(m_mutex);

    m_values[name] = value;
}

double kuge::Gauges::get(const std::string& name, double fallback) const
{
    std::lock_guard lock(m_mutex);
    const auto found = m_values.find(name);

    return found == m_values.end() ? fallback : found->second;
}

std::map<std::string, double> kuge::Gauges::all(void) const
{
    std::lock_guard lock(m_mutex);

    return m_values;
}
