#pragma once

#include "FixedTimestep.hpp"
#include "SceneManager.hpp"
#include "Stats.hpp"
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

            // -- Time, for a developer ------------------------------------------------------------
            //! No fixed tick runs (frames still do: the picture is drawn, the network is not polled) until it is false or
            //! advance() asks for some. Any thread.
            void setPaused(bool paused) noexcept { m_paused = paused; }
            bool paused(void) const noexcept { return m_paused; }

            //! The next `ticks` fixed ticks run even if paused; the loop then stays paused. Any thread.
            void advance(std::uint32_t ticks) noexcept { m_steps += ticks; }

            //! Time goes this much faster (2) or slower (0.25): the fixed step keeps its size, ticks come more or less often,
            //! so the simulation gives the same result after the same number of ticks. Between 0 and 100; 1 is real time.
            void   setTimeScale(double scale) noexcept { m_scale = scale < 0.0 ? 0.0 : (scale > 100.0 ? 100.0 : scale); }
            double timeScale(void) const noexcept { return m_scale; }

            //! How the loop goes (frames per second, ticks, work)
            const FrameStats& stats(void) const noexcept { return m_stats; }

        private:
            Time           m_time;
            FixedTimestep  m_timestep;
            std::uint64_t  m_ticksRun = 0;
            std::atomic<bool>          m_paused{false};
            std::atomic<std::uint32_t> m_steps{0};
            std::atomic<double>        m_scale{1.0};
            FrameStats     m_stats;
            SceneManager   m_scenes;   // last: destroyed first, its scenes use the rest
    };

}
