#pragma once

#include "ClientModule.hpp"
#include "Commands.hpp"

namespace kuge
{

    //! Commands of the client for a console: `screenshot [file]` writes what the next frame draws to a PPM file (screenshot_<n>.ppm in
    //! the current folder if no name is given) while playing.
    void registerClientCommands(ClientModule& client, Commands& commands);

}
