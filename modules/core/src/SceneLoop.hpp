#pragma once

#include "FixedTimestep.hpp"
#include "SceneManager.hpp"
#include "Time.hpp"
#include <atomic>
#include <cstdint>

namespace kuge
{

    class Engine;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  A stack of scenes and its clock: what makes them run at a fixed
     *         rate. The engine has one for its main scenes, and one for each
     *         scene it spawns.
     *
     * It knows nothing of threads: whoever owns it (the engine, a thread, the
     * pool) calls step() when it is time, and says how much time has passed.
     * It is used by one thread at a time.
     */
    ////////////////////////////////////////////////////////////////////////////
    class SceneLoop
    {
        public:
            SceneLoop(Engine& engine, std::uint32_t tickRate, std::uint32_t maxCatchUp, SceneHandle parent, bool mainThread);

            SceneLoop(const SceneLoop&)            = delete;
            SceneLoop& operator=(const SceneLoop&) = delete;

            SceneManager& scenes(void) noexcept { return m_scenes; }
            const Time&   time(void) const noexcept { return m_time; }

            //! Does the transitions that were asked
            //! @return  true if a scene is there to run
            bool ready(void);

            //! What one loop is: the messages, the ticks that the time asks for (stopping
            //! at once if stop becomes true), one frame
            void run(double frameSeconds, const std::atomic<bool>& stop);

            //! Does the transitions that were asked during run()
            void settle(void);

            //! ready(), run() and settle() when there is something to run
            //! @return  false when there is no scene left, or stop is true
            bool step(double frameSeconds, const std::atomic<bool>& stop);

            //! Seconds until the next tick is due
            double untilNextTick(void) const noexcept { return m_timestep.untilNextTick(); }

            //! Leaves every scene now
            void finish(void) { m_scenes.clear(); }

        private:
            Time           m_time;
            FixedTimestep  m_timestep;
            std::uint64_t  m_ticksRun = 0;
            SceneManager   m_scenes;   // last: destroyed first, its scenes use the rest
    };

}
