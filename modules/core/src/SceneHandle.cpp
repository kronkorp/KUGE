#include "SceneHandle.hpp"

bool kuge::SceneHandle::post(Message message) const
{
    return m_mailbox && m_mailbox->post(std::move(message));
}

bool kuge::SceneHandle::stop(void) const
{
    return post(Message(StopRequest{}));
}

bool kuge::SceneHandle::alive(void) const
{
    return m_mailbox && !m_mailbox->closed();
}
