#pragma once

#include "../base/IHandler.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

////////////////////////////////////////////////////////////////////////////
/**
 * @brief  Keeps the last lines of the log, for a panel of the game to show
 *
 *     auto ring = std::make_shared<RingLogHandler>(500);
 *     const auto id = Logger::logger().addHandler(ring);
 *     ...
 *     lines = ring->snapshot(seen);          // once per frame, on the UI's own thread
 *     if (!lines.empty()) seen = lines.back().id;
 *     ...
 *     Logger::logger().removeHandler(id);    // when the panel goes
 *
 * Thread-safe: the logger writes from any thread, the UI reads from its own, so no UI code runs on a logging thread.
 */
////////////////////////////////////////////////////////////////////////////
class RingLogHandler : public ILoggerHandler
{
public:
    struct Line
    {
        std::uint64_t id = 0;   //!< 1 for the first line ever kept, then 2... (never reused: see snapshot())
        LogRecord     record;
    };

    explicit RingLogHandler(std::size_t capacity) : m_capacity(capacity ? capacity : 1) {}

    void log(LoggerLevel level, const std::string& message) override { handle({level, timeToStr(), {}, message}); }

    void handle(const LogRecord& record) override
    {
        std::lock_guard lock(m_mutex);

        m_lines.push_back({++m_last, record});
        if (m_lines.size() > m_capacity) {
            m_lines.pop_front();
        }
    }

    //! The lines newer than `since` (an id from a previous call; 0: all that are kept), oldest first
    std::vector<Line> snapshot(std::uint64_t since = 0) const
    {
        std::lock_guard lock(m_mutex);
        std::vector<Line> lines;

        for (const Line& line : m_lines) {
            if (line.id > since) {
                lines.push_back(line);
            }
        }
        return lines;
    }

    //! The id of the last line (0 before any)
    std::uint64_t lastId(void) const
    {
        std::lock_guard lock(m_mutex);

        return m_last;
    }

private:
    mutable std::mutex m_mutex;
    std::deque<Line>   m_lines;
    std::size_t        m_capacity;
    std::uint64_t      m_last = 0;
};
