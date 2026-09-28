#pragma once

#include "Message.hpp"
#include <cstddef>
#include <mutex>
#include <vector>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The messages that wait for a scene: any thread posts, the scene
     *         takes them all at the start of its loop
     *
     * It is bounded: when it is full, a message is refused (post() says so, and
     * dropped() counts them) instead of growing without end when the receiver
     * is slow. Once closed (the scene is gone), nothing is accepted.
     *
     * Messages of one sender arrive in the order they were sent.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Mailbox
    {
        public:
            explicit Mailbox(std::size_t capacity = 4096) noexcept : m_capacity(capacity) {}

            Mailbox(const Mailbox&)            = delete;
            Mailbox& operator=(const Mailbox&) = delete;

            //! Any thread. @return  false if it is closed or full: the message is lost
            bool post(Message message);

            //! What is waiting, oldest first (the receiver's thread)
            std::vector<Message> drain(void);

            //! Refuses what comes next. What is waiting can still be drained.
            void close(void);

            bool        closed(void) const;
            std::size_t size(void) const;
            //! How many messages were refused because it was full
            std::size_t dropped(void) const;

        private:
            const std::size_t     m_capacity;
            mutable std::mutex    m_mutex;
            std::vector<Message>  m_messages;
            bool                  m_closed  = false;
            std::size_t           m_dropped = 0;
    };

}
