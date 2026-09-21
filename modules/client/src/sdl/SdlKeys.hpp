#pragma once

#include "input/Key.hpp"

namespace kuge
{

    //! The key at the place of an SDL scancode (SDL_Scancode), or Key::Unknown
    Key keyFromScancode(int scancode) noexcept;

}
