#pragma once

#include "Wire.hpp"
#include <string>

namespace kuge::net
{

    //! The room starts a cinematic that stops the game: every client plays the timeline `id` (see RoomScene::playCinematic())
    struct CinematicStart
    {
        KUGE_MESSAGE(CinematicStart, id)
        std::string id;
    };

    //! A client played the cinematic to its end, or the player skipped it
    struct CinematicDone
    {
        KUGE_MESSAGE(CinematicDone, id)
        std::string id;
    };

    //! The room cuts the cinematic short (the time limit came): a client that still plays it skips it
    struct CinematicStop
    {
        KUGE_MESSAGE(CinematicStop, id)
        std::string id;
    };

}
