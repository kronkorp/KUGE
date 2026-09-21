#pragma once

#include "FixedTimestep.hpp"
#include "Module.hpp"
#include "SceneManager.hpp"
#include "Time.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Runs the scenes: a fixed-rate simulation, and one frame per loop
     *
     * Each loop, the engine runs as many fixed ticks as the time that passed
     * asks for (at most Config::maxCatchUp), then one frame, then does the
     * scene transitions that were asked. Only the top scene runs. The modules
     * of the engine (see Module) take part in each loop.
     *
     *     kuge::Engine engine({.tickRate = 60});
     *     engine.scenes().change<MyScene>();
     *     return engine.run();
     */
    ////////////////////////////////////////////////////////////////////////////
    class Engine
    {
        public:
            enum class Mode {
                Headless,   //!< No window, no rendering (servers, tests)
                Windowed,   //!< With a window (needs the client module)
            };

            struct Config {
                Mode          mode       = Mode::Headless;
                std::uint32_t tickRate   = 60;   //!< Fixed ticks per second
                std::uint32_t maxCatchUp = 5;    //!< Max ticks run in one loop
                std::uint32_t maxFps     = 0;    //!< Loops per second (0: one per tick)
            };

            Engine(void);
            explicit Engine(Config config);
            ~Engine(void);

            Engine(const Engine&)            = delete;
            Engine& operator=(const Engine&) = delete;

            //! Adds a module, built with args. The engine owns it.
            //! @return  The module
            template<typename M, typename ...Args>
            M& addModule(Args&&... args)
            {
                static_assert(std::is_base_of_v<Module, M>, "a module must derive from kuge::Module");
                auto module = std::make_unique<M>(std::forward<Args>(args)...);
                M& added = *module;

                module->onAttach(*this);
                m_modules.push_back(std::move(module));
                return added;
            }

            //! The first module of type M, or nullptr
            template<typename M>
            M* module(void) noexcept
            {
                for (auto& candidate : m_modules) {
                    if (auto* found = dynamic_cast<M*>(candidate.get())) {
                        return found;
                    }
                }
                return nullptr;
            }

            //! Gives a World what the modules provide (done for each scene entered)
            void inject(kw::World& world);

            SceneManager&  scenes(void) noexcept;
            const Time&    time(void) const noexcept;
            const Config&  config(void) const noexcept;

            //! Runs until stop() is called, SIGINT / SIGTERM is received, or
            //! there is no scene left. Every scene is left before it returns.
            //! @return  0
            int run(void);

            //! Asks run() to end. Can be called from any thread, or a signal.
            void stop(void) noexcept;

            //! One loop, with a frame that lasted frameSeconds: what run() does
            //! with the real clock, and what tests do with a made-up one.
            //! @return  false when the engine has nothing left to run
            bool step(double frameSeconds);

        private:
            Config             m_config;
            FixedTimestep      m_timestep;
            Time               m_time;
            std::uint64_t      m_ticksRun = 0;
            std::atomic<bool>  m_stop{false};
            std::vector<std::unique_ptr<Module>> m_modules;
            SceneManager       m_scenes;   // last: destroyed first, its scenes use the rest
    };

}
