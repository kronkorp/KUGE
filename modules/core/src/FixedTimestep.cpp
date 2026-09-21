#include "FixedTimestep.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    // 60 frames of 1/60 s must give exactly 60 ticks, not 59 because of the
    // rounding of the additions
    constexpr double EPSILON = 1e-9;

    // Beyond that a frame is nonsense, and the conversion to integer is not safe
    constexpr double MAX_TICKS = 1e15;
}

kuge::FixedTimestep::FixedTimestep(
    std::uint32_t tickRate,
    std::uint32_t maxCatchUp
) : m_tickRate(tickRate), m_maxCatchUp(maxCatchUp)
{
    if (tickRate == 0) {
        throw std::invalid_argument("FixedTimestep: tickRate must be > 0");
    }
    if (maxCatchUp == 0) {
        throw std::invalid_argument("FixedTimestep: maxCatchUp must be > 0");
    }
}

std::uint32_t kuge::FixedTimestep::tickRate(void) const noexcept
{
    return m_tickRate;
}

double kuge::FixedTimestep::dt(void) const noexcept
{
    return 1.0 / m_tickRate;
}

std::uint32_t kuge::FixedTimestep::advance(
    double frameSeconds
) noexcept
{
    double whole;
    std::uint64_t due;
    std::uint64_t run;

    if (!(frameSeconds > 0.0) || !std::isfinite(frameSeconds)) {
        return 0;
    }
    m_ticks += frameSeconds * m_tickRate;
    whole = std::min(std::floor(m_ticks + EPSILON), MAX_TICKS);
    m_ticks = std::max(m_ticks - whole, 0.0);
    due = static_cast<std::uint64_t>(whole);
    run = std::min<std::uint64_t>(due, m_maxCatchUp);
    m_dropped += due - run;
    return static_cast<std::uint32_t>(run);
}

double kuge::FixedTimestep::alpha(void) const noexcept
{
    return m_ticks;
}

double kuge::FixedTimestep::untilNextTick(void) const noexcept
{
    return (1.0 - m_ticks) / m_tickRate;
}

std::uint64_t kuge::FixedTimestep::dropped(void) const noexcept
{
    return m_dropped;
}
