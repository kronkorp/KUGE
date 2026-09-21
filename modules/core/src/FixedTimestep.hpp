#pragma once

#include <cstdint>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Turns the real time of each frame into a number of fixed ticks
     *
     * It knows nothing about clocks: the caller gives it the duration of the
     * last frame, which makes it testable and reproducible.
     *
     * A frame that lasts too long (breakpoint, window dragged, machine busy)
     * would ask for a lot of ticks, which take time, which makes the next frame
     * longer still. At most maxCatchUp ticks are run per frame, the rest of the
     * time is dropped (see dropped()).
     */
    ////////////////////////////////////////////////////////////////////////////
    class FixedTimestep
    {
        public:
            //! @throw std::invalid_argument if tickRate or maxCatchUp is 0
            FixedTimestep(std::uint32_t tickRate, std::uint32_t maxCatchUp);

            std::uint32_t tickRate(void) const noexcept;

            //! Seconds per tick
            double dt(void) const noexcept;

            //! Adds the duration of a frame. Returns how many ticks to run now.
            //! A negative, NaN or infinite duration counts for nothing.
            std::uint32_t advance(double frameSeconds) noexcept;

            //! Part of a tick already elapsed, in [0, 1)
            double alpha(void) const noexcept;

            //! Seconds before the next tick is due
            double untilNextTick(void) const noexcept;

            //! Ticks that were not run because of maxCatchUp
            std::uint64_t dropped(void) const noexcept;

        private:
            std::uint32_t m_tickRate;
            std::uint32_t m_maxCatchUp;
            double        m_ticks   = 0.0;   //!< Time not turned into ticks yet, in ticks
            std::uint64_t m_dropped = 0;
    };

}
