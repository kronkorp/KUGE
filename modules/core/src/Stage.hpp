#pragma once

#include "kronkworld/system/System.hpp"

namespace kuge::stage
{

    // Phases of a tick, in the order they run (a lower stage runs first).
    // Systems of a same stage run in the order they were added.
    constexpr kw::StageId Network     = 0;   //!< Receive from the network
    constexpr kw::StageId Input       = 1;   //!< Read inputs / apply received actions
    constexpr kw::StageId Simulation  = 2;   //!< Movement wishes, AI, game rules
    constexpr kw::StageId Physics     = 3;   //!< Things move and collide (kuge-physics)
    constexpr kw::StageId Late        = 4;   //!< React to what the physics found, damage...
    constexpr kw::StageId Replication = 5;   //!< Send the state to the clients
    constexpr kw::StageId Render      = 6;   //!< Draw (Frame schedule)

}
