#include "TickDriver.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <exception>

kuge::TickDriver::TickDriver(ThreadPool& pool) : m_pool(pool), m_thread([this] { driverMain(); })
{
}

kuge::TickDriver::~TickDriver(void)
{
    stopAll();
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_cond.notify_all();
    m_thread.join();
}

void kuge::TickDriver::add(std::unique_ptr<SceneLoop> loop)
{
    auto item = std::make_unique<Item>();

    item->loop = std::move(loop);
    item->last = Clock::now();
    item->due = item->last;
    {
        std::lock_guard lock(m_mutex);
        m_items.push_back(std::move(item));
    }
    m_cond.notify_all();
}

void kuge::TickDriver::stopAll(void)
{
    std::unique_lock lock(m_mutex);

    for (auto& item : m_items) {
        item->stopping = true;
    }
    m_cond.notify_all();
    // The driver gives each loop a last tick that leaves its scenes
    m_cond.wait(lock, [this] {
        return std::all_of(m_items.begin(), m_items.end(), [](const auto& item) { return item->finished && !item->running; });
    });
}

std::size_t kuge::TickDriver::active(void) const
{
    std::lock_guard lock(m_mutex);

    return static_cast<std::size_t>(std::count_if(m_items.begin(), m_items.end(), [](const auto& item) { return !item->finished; }));
}

void kuge::TickDriver::driverMain(void)
{
    std::unique_lock lock(m_mutex);

    while (!m_quit) {
        const auto now = Clock::now();
        auto wakeAt = now + std::chrono::milliseconds(100);   // (a safety net: everything that matters notifies)

        std::erase_if(m_items, [](const auto& item) { return item->finished && !item->running; });
        for (auto& item : m_items) {
            if (item->running || item->finished) {
                continue;
            }
            if (item->stopping || item->due <= now) {
                item->running = true;
                try {
                    Item* raw = item.get();

                    m_pool.post([this, raw] { tick(*raw); });
                } catch (const std::exception& e) {
                    Logger::logger().error("A scene cannot be run: {}", e.what());
                    item->running = false;
                    item->finished = true;
                }
            } else {
                wakeAt = std::min(wakeAt, item->due);
            }
        }
        m_cond.wait_until(lock, wakeAt);
    }
}

// Runs on a worker
void kuge::TickDriver::tick(Item& item)
{
    bool over = false;

    try {
        if (item.stopping) {
            item.loop->finish();
            over = true;
        } else {
            const auto now = Clock::now();
            const double frame = std::chrono::duration<double>(now - item.last).count();

            item.last = now;
            over = !item.loop->step(frame, item.stopping);
            if (over) {
                item.loop->finish();
            }
        }
    } catch (const std::exception& e) {
        Logger::logger().error("A scene threw and is stopped: {}", e.what());
        over = true;
    } catch (...) {
        Logger::logger().error("A scene threw and is stopped");
        over = true;
    }
    if (over) {
        try {
            item.loop->finish();
        } catch (...) {
            Logger::logger().error("A scene threw while being left");
        }
    }
    {
        std::lock_guard lock(m_mutex);

        item.finished = over;
        item.running = false;
        item.due = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(item.loop->untilNextTick()));
        // Under the lock: once it is released, the driver may be gone (see the destructor)
        m_cond.notify_all();
    }
}
