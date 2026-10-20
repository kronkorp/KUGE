#pragma once

#include "Math2D.hpp"
#include <string>

namespace kuge
{

    //! What a game asks of its window, whatever library shows it
    struct WindowConfig
    {
        std::string title      = "KUGE";
        int         width      = 1280;
        int         height     = 720;
        bool        resizable  = false;
        bool        fullscreen = false;
        bool        vsync      = true;
        //! Also make an OpenGL 3.3 context for the game to draw 3D with (Backend::opengl). If the machine has none
        //! the backend is given without, and says so in the log.
        bool        openGL     = false;
    };

    //! The window of the game. A backend gives one (see IRenderer2D).
    class IWindow
    {
        public:
            virtual ~IWindow(void) = default;

            //! The size of what can be drawn, in pixels
            virtual Vec2 size(void) const = 0;

            virtual void setTitle(const std::string& title) = 0;

            //! The text in the system's clipboard ("" if it is empty or is not text)
            virtual std::string clipboardText(void) const = 0;
            virtual void setClipboardText(const std::string& text) = 0;
    };

}
