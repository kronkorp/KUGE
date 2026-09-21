#pragma once

#include "Mailbox.hpp"
#include <memory>
#include <utility>

namespace kuge
{

    class Scene;

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  How to talk to a scene that may run on another thread: it can
     *         only be sent messages
     *
     * A handle can be copied and sent to anyone, and outlives the scene it
     * points to (then send() just says no). It never gives access to the scene
     * itself or to its World: nothing is shared between scenes.
     *
     *     kuge::SceneHandle room = ctx().spawn<RoomScene>(kuge::RunPolicy::Dedicated, settings);
     *     room.send(StartMatch{});
     *     ...
     *     room.stop();
     */
    ////////////////////////////////////////////////////////////////////////////
    class SceneHandle
    {
        public:
            SceneHandle(void) = default;

            //! Any thread. @return  false if the scene is gone (or the handle empty), or its mailbox is full
            template<typename T>
            bool send(T&& value) const
            {
                return post(Message(std::forward<T>(value)));
            }

            bool post(Message message) const;

            //! Asks the scene to end: it is popped from its stack, at the start of its next loop
            bool stop(void) const;

            //! Points to a scene (even one that is gone)
            bool valid(void) const noexcept { return static_cast<bool>(m_mailbox); }

            //! Points to a scene that is still there
            bool alive(void) const;

            bool operator==(const SceneHandle& other) const noexcept { return m_mailbox == other.m_mailbox; }

        private:
            friend class Scene;

            explicit SceneHandle(std::shared_ptr<Mailbox> mailbox) noexcept : m_mailbox(std::move(mailbox)) {}

            std::shared_ptr<Mailbox> m_mailbox;
    };

}
