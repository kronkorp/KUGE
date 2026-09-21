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
    //! The sound is optional: on a machine with no sound card, the backend is
    //! given without audio (and says so in the log) and the game is silent.
    Backend makeSdlBackend(const WindowConfig& config = {});

    //! Fonts (TrueType, OpenType...) with SDL_ttf
    std::unique_ptr<IFontLoader> makeSdlFontLoader(void);

    //! Only the sound, with SDL_mixer: WAV, and OGG, MP3, FLAC when the decoders are there
    //! @throw AudioError if no audio device can be opened
    std::unique_ptr<IAudio> makeSdlAudio(void);

}
