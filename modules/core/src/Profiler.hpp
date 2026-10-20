#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kuge
{

    namespace detail
    {
        //! Is profiling on? One relaxed read is all a scope costs when it is off.
        inline std::atomic<bool> g_profiling{false};
    }

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Times the systems of every scene and the scopes you mark, and exports a trace
     *
     *     kuge::Profiler::instance().enable(true);          // or the command `profile start`
     *     ...
     *     void Chase::handle(...) { KUGE_PROFILE_SCOPE("chase: pick targets"); ... }
     *     ...
     *     kuge::Profiler::instance().saveTrace("trace.json");   // open it in https://ui.perfetto.dev or chrome://tracing
     *
     * **Every system** of every scene is timed by the scene (by its class name, in the category of its stage), with no
     * line in the games. Events go to a buffer of the thread that makes them (the last 32768 of each: older ones are
     * overwritten), so scenes on several threads do not mix. With profiling off nothing is recorded.
     *
     * Reading (events(), top(), traceJson()) takes the buffers one after the other: do it from a thread that is not
     * in a hurry (a command, the overlay).
     */
    ////////////////////////////////////////////////////////////////////////////
    class Profiler
    {
        public:
            struct Event
            {
                const char*   name;       //!< Never freed
                const char*   category;
                std::uint64_t start;      //!< Nanoseconds, from the clock of the profiler
                std::uint64_t end;
                std::uint32_t thread;     //!< A small number per thread, in the order they first recorded
            };

            //! What a name cost over the events asked for
            struct Row
            {
                std::string   name;
                std::string   category;
                std::size_t   calls = 0;
                std::uint64_t total = 0;   //!< Nanoseconds
                std::uint64_t worst = 0;
            };

            static Profiler& instance(void);

            static bool enabled(void) noexcept { return detail::g_profiling.load(std::memory_order_relaxed); }
            void        enable(bool on) noexcept { detail::g_profiling.store(on, std::memory_order_relaxed); }

            //! Nanoseconds. The steady clock unless a test gives another (set it while nothing is being recorded).
            std::uint64_t now(void) const;
            void          setClock(std::function<std::uint64_t(void)> clock);

            //! A text that lives for ever, whose pointer is the same for the same text (names of systems are made once)
            static const char* intern(std::string_view text);

            void record(const char* name, const char* category, std::uint64_t start, std::uint64_t end);

            //! Every event kept, by start (ties: by thread)
            std::vector<Event> events(void) const;

            //! The names that cost the most (total time) among the events that began at `since` or later
            std::vector<Row> top(std::size_t count, std::uint64_t since = 0) const;

            void clear(void);

            //! Chrome's trace format: a JSON array of complete events ("ph": "X"), times in microseconds
            std::string traceJson(void) const;

            //! @throw std::runtime_error if the file cannot be written
            void saveTrace(const std::filesystem::path& file) const;

        private:
            Profiler(void) = default;

            struct Impl;
            Impl& impl(void) const;
    };

    //! Records the time between its construction and its destruction (nothing when profiling is off)
    class ProfileScope
    {
        public:
            explicit ProfileScope(const char* name, const char* category = "scope") noexcept
            {
                if (Profiler::enabled()) {
                    m_name = name;
                    m_category = category;
                    m_start = Profiler::instance().now();
                }
            }

            ~ProfileScope(void)
            {
                if (m_name) {
                    Profiler::instance().record(m_name, m_category, m_start, Profiler::instance().now());
                }
            }

            ProfileScope(const ProfileScope&)            = delete;
            ProfileScope& operator=(const ProfileScope&) = delete;

        private:
            const char*   m_name = nullptr;
            const char*   m_category = nullptr;
            std::uint64_t m_start = 0;
    };

}

#define KUGE_PROFILE_CONCAT_(a, b) a##b
#define KUGE_PROFILE_CONCAT(a, b) KUGE_PROFILE_CONCAT_(a, b)

//! The same, with a category of your own (a name and a category that are pointers that stay valid)
#define KUGE_PROFILE_SCOPE_AS(name, category) ::kuge::ProfileScope KUGE_PROFILE_CONCAT(kugeProfileScope, __LINE__)(name, category)

//! Times the rest of the block under this name (a string literal, or a text from Profiler::intern())
#define KUGE_PROFILE_SCOPE(name) ::kuge::ProfileScope KUGE_PROFILE_CONCAT(kugeProfileScope, __LINE__)(name)
