#pragma once

#include "backend/IInputSource.hpp"
#include "backend/IRenderer2D.hpp"
#include "backend/IWindow.hpp"
#include <memory>
#include <stdexcept>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What the client needs from the outside: a window, its inputs, and
     *         a way to draw in it
     *
     * The engine brings one built on SDL2 (makeSdlBackend()) and one that does
     * nothing (makeDummyBackend(), for tests). A game that prefers another
     * library implements the three interfaces on top of it and gives them here.
     */
    ////////////////////////////////////////////////////////////////////////////
    //! A backend that cannot be started (no display, no graphics...)
    class BackendError : public std::runtime_error
    {
        public:
            using std::runtime_error::runtime_error;
    };

    struct Backend
    {
        std::unique_ptr<IWindow>      window;
        std::unique_ptr<IInputSource> input;
        std::unique_ptr<IRenderer2D>  renderer;
    };

}
