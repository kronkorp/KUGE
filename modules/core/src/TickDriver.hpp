#pragma once

#include "SceneLoop.hpp"
#include "ThreadPool.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Runs many scene loops on few threads: one thread works out when
     *         each loop is due, and gives its tick to a worker of the pool
     *
     * - A loop never runs twice at once: while one of its ticks is still running,
     *   the next is not given out.
     * - A loop that is late does not pile up work: what it missed is time it
     *   catches up in its next loop (at most maxCatchUp ticks, see FixedTimestep).
     * - A loop that has no scene left, or that throws, is left and forgotten.
     *
     * Its scenes enter, run and are left on the pool's threads.
     */
    ////////////////////////////////////////////////////////////////////////////
    class TickDriver
    {
        public:
            explicit TickDriver(ThreadPool& pool);
            ~TickDriver(void);

            TickDriver(const TickDriver&)            = delete;
            TickDriver& operator=(const TickDriver&) = delete;

            //! Any thread
            void add(std::unique_ptr<SceneLoop> loop);

            //! Leaves every scene (on the pool) and returns when they are all gone
            void stopAll(void);

            //! Loops that still run
            std::size_t active(void) const;

        private:
            using Clock = std::chrono::steady_clock;

            struct Item
            {
                std::unique_ptr<SceneLoop>  loop;
                std::atomic<bool>           stopping{false};
                bool                        running  = false;   // under m_mutex
                bool                        finished = false;   // under m_mutex
                Clock::time_point           last;               // only the tick that runs touches it
                Clock::time_point           due;                // under m_mutex
            };

            void driverMain(void);
            void tick(Item& item);

            ThreadPool&                         m_pool;
            mutable std::mutex                  m_mutex;
            std::condition_variable             m_cond;
            std::vector<std::unique_ptr<Item>>  m_items;
            bool                                m_quit = false;
            std::thread                         m_thread;
    };

}
