#pragma once

#include <string>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  An OpenGL 3.3 (core) context that a game draws in by itself, as kronk3d does with its GPU
     *         rasterizer: a backend can give one next to its 2D renderer (see Backend::opengl)
     *
     * It is not the window's: it has a hidden surface of its own, so the 2D renderer and this one never meet
     * (the 3D is read back and shown through the 2D renderer, which keeps sprites, text, the interface and the
     * screenshots working as they are). The 2D renderer may take the context of the thread: call makeCurrent()
     * before drawing.
     */
    ////////////////////////////////////////////////////////////////////////////
    class IOpenGL
    {
        public:
            virtual ~IOpenGL(void) = default;

            //! Makes it the context of the calling thread
            virtual void makeCurrent(void) = 0;

            //! The function of the driver of this name (what an OpenGL loader needs), null if it has none
            virtual void (*procAddress(const char* name))(void) = 0;

            //! Gives its surface the size of the screen
            virtual void resize(int width, int height) = 0;

            //! What draws ("NVIDIA GeForce RTX 4070 ..., OpenGL 3.3"), for the log. The context must be current.
            virtual std::string description(void) = 0;
    };

}
