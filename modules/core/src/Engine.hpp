#pragma once

#include "Module.hpp"
#include "SceneContext.hpp"
#include "SceneLoop.hpp"
#include "SceneManager.hpp"
#include "StopSignal.hpp"
#include "ThreadPool.hpp"
#include "Time.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace kuge
{

    class TickDriver;

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
     *
     * **Threads.** The scenes above are the main ones, on the thread that calls
     * run(). Other scenes can be spawned (see spawn() and RunPolicy): on a thread
     * of their own, on the worker threads, or on the main thread next to the main
     * scenes. Each has its own World and its own loop, and they only talk through
     * messages (see SceneHandle). They are all stopped when run() ends.
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
                std::uint32_t workers    = 0;    //!< Worker threads (pool: pooled scenes, background jobs). 0: as many as the CPU has
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
            //! @param mainThread  false: the scene runs on another thread, and only gets what
            //!                    the modules say they can share (Module::sharedAcrossThreads)
            void inject(kw::World& world, bool mainThread = true);

            //! Starts a scene that runs as the policy says, next to the main ones. Any thread.
            //! It is built here, with args, and entered by the thread that runs it.
            //! @return  How to talk to it (see also SceneContext::spawn(), which sets its parent)
            template<typename T, typename ...Args>
            SceneHandle spawn(RunPolicy policy, Args&&... args)
            {
                static_assert(std::is_base_of_v<Scene, T>, "a scene must derive from kuge::Scene");
                return spawnScene(policy, std::make_unique<T>(std::forward<Args>(args)...), SceneHandle());
            }

            //! Same, for a scene that is already made
            SceneHandle spawnScene(RunPolicy policy, std::unique_ptr<Scene> scene, SceneHandle parent);

            //! The worker threads. Made on first use.
            ThreadPool& pool(void);

            //! Spawned scenes that still run
            std::size_t spawned(void);

            SceneManager&  scenes(void) noexcept;
            //! The clock of the main scenes
            const Time&    time(void) const noexcept;
            const Config&  config(void) const noexcept;

            //! Runs until stop() is called, SIGINT / SIGTERM is received (Ctrl+C or
            //! Ctrl+Break on Windows), or there is no scene left. Every scene is left
            //! before it returns.
            //! @return  0
            int run(void);

            //! Asks run() to end. Can be called from any thread, or a signal.
            void stop(void) noexcept;

            //! One loop, with a frame that lasted frameSeconds: what run() does
            //! with the real clock, and what tests do with a made-up one.
            //! @return  false when the engine has nothing left to run
            bool step(double frameSeconds);

        private:
            //! A scene loop on a thread of its own
            struct Dedicated
            {
                std::unique_ptr<SceneLoop>  loop;
                std::thread                 thread;
                std::atomic<bool>           finished{false};
            };

            void stepSideLoops(double frameSeconds);
            void reap(void);
            void stopSpawned(void);
            void runDedicated(Dedicated& runner);
            TickDriver& driver(void);

            Config             m_config;
            std::atomic<bool>  m_stop{false};
            std::vector<std::unique_ptr<Module>> m_modules;

            // What is spawned. Everything here is under m_spawnMutex, except the main-thread
            // loops once they are taken by stepSideLoops (only the main thread touches them).
            std::mutex                               m_spawnMutex;
            std::atomic<bool>                        m_stopSpawned{false};
            StopSignal                               m_wake;   // wakes the dedicated threads that sleep
            std::vector<std::unique_ptr<Dedicated>>  m_dedicated;
            std::vector<std::unique_ptr<SceneLoop>>  m_sideAdded;
            std::vector<std::unique_ptr<SceneLoop>>  m_side;
            std::unique_ptr<ThreadPool>              m_pool;
            std::unique_ptr<TickDriver>              m_driver;

            SceneLoop          m_main;   // last: destroyed first, its scenes use the rest
    };

}
