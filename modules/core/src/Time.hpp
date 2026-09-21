#pragma once

#include <cstdint>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The clock of the engine, given to every scene as a resource of its
     *         World (world.getResource<kuge::Time>())
     *
     * Simulation runs at a fixed rate: in the Fixed schedule, dt is always the
     * same and tick counts the ticks already run. In the Frame schedule, alpha
     * says how far the frame is between two ticks (0 = the last tick, 1 = the
     * next one), to draw interpolated positions.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Time
    {
        double        dt       = 1.0 / 60.0;  //!< Seconds per fixed tick
        std::uint32_t tickRate = 60;          //!< Fixed ticks per second
        std::uint64_t tick     = 0;           //!< Index of the fixed tick running
        double        alpha    = 0.0;         //!< [0, 1) between two ticks (Frame)
        double        frameDt  = 0.0;         //!< Real seconds since the last frame (Frame)
    };

}
