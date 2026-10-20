#pragma once

#include "Commands.hpp"
#include "Engine.hpp"
#include <functional>
#include <string>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The commands of the engine itself, for a console (see Commands): time, the profiler, the frame statistics
     *
     *     pause [on|off]        stops the fixed ticks (frames are still drawn); with no word it flips
     *     step [n]              runs n fixed ticks (1) and stays paused
     *     timescale <x>         0.25 is four times slower, 2 twice as fast; the step keeps its size, so the simulation does not change
     *     profile start|stop|clear|top [n]|save <file>     times every system (see Profiler); save writes a Chrome trace
     *     fps                   frames per second, the mean and the worst frame, the cost of a tick
     *
     * @param refuse  Asked before a command that changes time: the reason it is not allowed now, or "" to allow it. On a server
     *                (Cvars::Side::Server) they are refused unless cheats are on; a game adds its own rule here ("only
     *                with one player in the room").
     */
    ////////////////////////////////////////////////////////////////////////////
    void registerEngineCommands(Engine& engine, Commands& commands, std::function<std::string(void)> refuse = {});

}
