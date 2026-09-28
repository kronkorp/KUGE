#pragma once

#include <cstddef>
#include <functional>

struct kronkpool_threadpool_s;

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Worker threads that run what they are given (kronkpool)
     *
     *     kuge::ThreadPool pool(4);
     *     pool.post([] { ... });         // runs on a worker
     *     pool.waitIdle();               // until everything given is done
     *
     * A task must not throw (it would end the process): catch inside it.
     * Destroying the pool finishes what was given first.
     */
    ////////////////////////////////////////////////////////////////////////////
    class ThreadPool
    {
        public:
            //! @param workers  0: as many as the machine has
            //! @throw std::runtime_error if the threads cannot be made
            explicit ThreadPool(std::size_t workers = 0);
            ~ThreadPool(void);

            ThreadPool(const ThreadPool&)            = delete;
            ThreadPool& operator=(const ThreadPool&) = delete;

            //! Any thread. @throw std::runtime_error if the pool refuses it
            void post(std::function<void(void)> task);

            //! Until no task is waiting or running (not from a task of this pool)
            void waitIdle(void);

            std::size_t workers(void) const noexcept;

        private:
            kronkpool_threadpool_s* m_pool;
    };

}
