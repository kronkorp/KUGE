#include "Profiler.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

namespace
{
    constexpr std::size_t CAPACITY = 32768;   // events kept by each thread

    void escape(std::string& out, const char* text)
    {
        for (const char* c = text; *c; ++c) {
            switch (*c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(*c) < 0x20) {
                        static const char* digits = "0123456789abcdef";

                        out += "\\u00";
                        out += digits[(*c >> 4) & 0xF];
                        out += digits[*c & 0xF];
                    } else {
                        out += *c;
                    }
            }
        }
    }
}

struct kuge::Profiler::Impl
{
    // The events of one thread: written by it, read by whoever exports. A ring.
    struct Buffer
    {
        std::mutex         mutex;
        std::vector<Event> ring;
        std::size_t        next = 0;       // once full: where the next one goes
        std::uint32_t      thread = 0;
    };

    mutable std::mutex                       mutex;       // the list of buffers, the clock and the interned texts
    std::vector<std::shared_ptr<Buffer>>     buffers;
    std::set<std::string, std::less<>>       texts;
    std::function<std::uint64_t(void)>       clock;
    std::atomic<bool>                        hasClock{false};   // so that now() takes no lock unless a test made its own clock

    Buffer& mine(void)
    {
        thread_local std::shared_ptr<Buffer> buffer;

        if (!buffer) {
            buffer = std::make_shared<Buffer>();
            std::lock_guard lock(mutex);

            buffer->thread = static_cast<std::uint32_t>(buffers.size());
            buffers.push_back(buffer);
        }
        return *buffer;
    }
};

kuge::Profiler::Impl& kuge::Profiler::impl(void) const
{
    static Impl instance;

    return instance;
}

kuge::Profiler& kuge::Profiler::instance(void)
{
    static Profiler profiler;

    return profiler;
}

std::uint64_t kuge::Profiler::now(void) const
{
    if (impl().hasClock.load(std::memory_order_acquire)) {
        std::lock_guard lock(impl().mutex);

        return impl().clock();
    }
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void kuge::Profiler::setClock(std::function<std::uint64_t(void)> clock)
{
    std::lock_guard lock(impl().mutex);

    impl().hasClock.store(false, std::memory_order_release);
    impl().clock = std::move(clock);
    impl().hasClock.store(static_cast<bool>(impl().clock), std::memory_order_release);
}

const char* kuge::Profiler::intern(std::string_view text)
{
    Impl& state = instance().impl();
    std::lock_guard lock(state.mutex);

    return state.texts.emplace(text).first->c_str();   // (a set does not move its elements)
}

void kuge::Profiler::record(const char* name, const char* category, std::uint64_t start, std::uint64_t end)
{
    Impl::Buffer& buffer = impl().mine();
    std::lock_guard lock(buffer.mutex);
    const Event event{name, category, start, end, buffer.thread};

    if (buffer.ring.size() < CAPACITY) {
        buffer.ring.push_back(event);
    } else {
        buffer.ring[buffer.next] = event;   // the oldest goes
        buffer.next = (buffer.next + 1) % CAPACITY;
    }
}

std::vector<kuge::Profiler::Event> kuge::Profiler::events(void) const
{
    std::vector<Event> all;
    std::vector<std::shared_ptr<Impl::Buffer>> buffers;
    {
        std::lock_guard lock(impl().mutex);

        buffers = impl().buffers;
    }
    for (const auto& buffer : buffers) {
        std::lock_guard lock(buffer->mutex);

        all.insert(all.end(), buffer->ring.begin(), buffer->ring.end());
    }
    std::sort(all.begin(), all.end(), [](const Event& a, const Event& b) {
        return a.start != b.start ? a.start < b.start : a.thread < b.thread;
    });
    return all;
}

std::vector<kuge::Profiler::Row> kuge::Profiler::top(std::size_t count, std::uint64_t since) const
{
    std::map<std::pair<std::string, std::string>, Row> rows;

    for (const Event& event : events()) {
        if (event.start < since) {
            continue;
        }
        Row& row = rows[{event.name, event.category}];
        const std::uint64_t took = event.end - event.start;

        row.name = event.name;
        row.category = event.category;
        ++row.calls;
        row.total += took;
        row.worst = std::max(row.worst, took);
    }
    std::vector<Row> result;

    for (auto& [key, row] : rows) {
        result.push_back(std::move(row));
    }
    std::sort(result.begin(), result.end(), [](const Row& a, const Row& b) { return a.total != b.total ? a.total > b.total : a.name < b.name; });
    if (result.size() > count) {
        result.resize(count);
    }
    return result;
}

void kuge::Profiler::clear(void)
{
    std::vector<std::shared_ptr<Impl::Buffer>> buffers;
    {
        std::lock_guard lock(impl().mutex);

        buffers = impl().buffers;
    }
    for (const auto& buffer : buffers) {
        std::lock_guard lock(buffer->mutex);

        buffer->ring.clear();
        buffer->next = 0;
    }
}

std::string kuge::Profiler::traceJson(void) const
{
    std::string json = "[";
    bool first = true;

    for (const Event& event : events()) {
        json += first ? "\n" : ",\n";
        first = false;
        json += "{\"name\":\"";
        escape(json, event.name);
        json += "\",\"cat\":\"";
        escape(json, event.category);
        json += "\",\"ph\":\"X\",\"ts\":" + std::to_string(static_cast<double>(event.start) / 1000.0)
            + ",\"dur\":" + std::to_string(static_cast<double>(event.end - event.start) / 1000.0)
            + ",\"pid\":1,\"tid\":" + std::to_string(event.thread) + "}";
    }
    json += "\n]\n";
    return json;
}

void kuge::Profiler::saveTrace(const std::filesystem::path& file) const
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);

    if (!out) {
        throw std::runtime_error("cannot write the trace '" + file.string() + "'");
    }
    out << traceJson();
    if (!out) {
        throw std::runtime_error("cannot write the trace '" + file.string() + "'");
    }
}
