#pragma once

#include "backend/Backend.hpp"

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The default backend: window, keyboard, mouse, gamepads and 2D
     *         drawing with SDL2
     *
     * Draws on the GPU when there is one, otherwise on the CPU. Textures are
     * not smoothed when they are scaled (pixel art stays sharp).
     *
     * @throw BackendError if there is nothing to show a window on
     */
    ////////////////////////////////////////////////////////////////////////////
    Backend makeSdlBackend(const WindowConfig& config = {});

}
