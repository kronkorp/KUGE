#include "Mailbox.hpp"

bool kuge::Mailbox::post(Message message)
{
    std::lock_guard lock(m_mutex);

    if (m_closed) {
        return false;
    }
    if (m_messages.size() >= m_capacity) {
        ++m_dropped;
        return false;
    }
    m_messages.push_back(std::move(message));
    return true;
}

std::vector<kuge::Message> kuge::Mailbox::drain(void)
{
    std::vector<Message> taken;

    {
        std::lock_guard lock(m_mutex);
        taken.swap(m_messages);
    }
    return taken;
}

void kuge::Mailbox::close(void)
{
    std::lock_guard lock(m_mutex);

    m_closed = true;
}

bool kuge::Mailbox::closed(void) const
{
    std::lock_guard lock(m_mutex);

    return m_closed;
}

std::size_t kuge::Mailbox::size(void) const
{
    std::lock_guard lock(m_mutex);

    return m_messages.size();
}

std::size_t kuge::Mailbox::dropped(void) const
{
    std::lock_guard lock(m_mutex);

    return m_dropped;
}
