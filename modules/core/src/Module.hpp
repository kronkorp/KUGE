#pragma once

#include "kronkworld/Kronkworld.hpp"

namespace kuge
{

    class Engine;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A part of the engine that a game adds to run (the client, the
     *         server...), without the core knowing about it
     *
     *     kuge::Engine engine({.mode = kuge::Engine::Mode::Windowed});
     *     engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend({}));
     *
     * A module lives as long as the engine, and is destroyed after all the
     * scenes (in the reverse order it was added). The engine calls it:
     *  - onAttach() once, when it is added;
     *  - inject() every time a scene is entered, before its onEnter(): put in
     *    the World the resources the systems of the scene need (see also
     *    sharedAcrossThreads());
     *  - beginFrame() before the fixed ticks of each loop (read the inputs...),
     *    and endFrame() after the frame (show what was drawn...).
     */
    ////////////////////////////////////////////////////////////////////////////
    class Module
    {
        public:
            virtual ~Module(void) = default;

            virtual void onAttach(Engine&) {}

            //! Can what inject() gives be used from any thread? A window, a renderer or a
            //! sound device cannot: a module that says no (the default) only injects into
            //! the scenes that run on the main thread, not into spawned ones on other threads.
            virtual bool sharedAcrossThreads(void) const noexcept { return false; }

            virtual void inject(kw::World&) {}
            virtual void beginFrame(Engine&) {}
            virtual void endFrame(Engine&) {}
    };

}
