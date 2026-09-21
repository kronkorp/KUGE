#pragma once

#include "kronkworld/system/System.hpp"

namespace kuge::stage
{

    // Phases of a tick, in the order they run (a lower stage runs first).
    // Systems of a same stage run in the order they were added.
    constexpr kw::StageId Network     = 0;   //!< Receive from the network
    constexpr kw::StageId Input       = 1;   //!< Read inputs / apply received actions
    constexpr kw::StageId Simulation  = 2;   //!< Movement, AI, game rules
    constexpr kw::StageId Late        = 3;   //!< Collisions, damage, after everything moved
    constexpr kw::StageId Replication = 4;   //!< Send the state to the clients
    constexpr kw::StageId Render      = 5;   //!< Draw (Frame schedule)

}
