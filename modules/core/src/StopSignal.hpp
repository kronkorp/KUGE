#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace kuge
{

    //! "Stop": what threads that sleep between two ticks wait on, so that they
    //! wake up at once instead of at the end of their sleep
    class StopSignal
    {
        public:
            void request(void)
            {
                {
                    std::lock_guard lock(m_mutex);
                    m_requested = true;
                }
                m_cond.notify_all();
            }

            void reset(void)
            {
                std::lock_guard lock(m_mutex);
                m_requested = false;
            }

            bool requested(void) const
            {
                std::lock_guard lock(m_mutex);
                return m_requested;
            }

            //! Sleeps for at most `seconds`, or until stop is requested
            //! @return  true if it is requested
            bool waitFor(double seconds)
            {
                std::unique_lock lock(m_mutex);

                m_cond.wait_for(lock, std::chrono::duration<double>(seconds), [this] { return m_requested; });
                return m_requested;
            }

        private:
            mutable std::mutex      m_mutex;
            std::condition_variable m_cond;
            bool                    m_requested = false;
    };

}
