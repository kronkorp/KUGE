extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Logger.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    constexpr int THREADS = 8;
    constexpr int LINES   = 500;

    std::vector<std::string> linesOf(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream in(text);
        std::string line;

        while (std::getline(in, line)) {
            lines.push_back(line);
        }
        return lines;
    }

    // What a line says: what comes after " >> "
    std::string saidBy(const std::string& line)
    {
        const auto at = line.find(" >> ");

        return at == std::string::npos ? std::string() : line.substr(at + 4);
    }
}

Test(logger, one_thread)
{
    Logger logger;
    auto out = std::make_shared<std::ostringstream>();

    logger.registerHandler(out);
    logger.setLevel(LoggerLevel::DEBUG);
    logger.info("hello {}", 42);
    logger.warn("careful");
    const auto lines = linesOf(out->str());

    AssertEq(lines.size(), 2, "two lines");
    AssertStrEq(saidBy(lines[0]).c_str(), "hello 42", "formatted");
    AssertStrEq(saidBy(lines[1]).c_str(), "careful", "the next one");
}

// Text without arguments is not a format: a message that comes from somewhere else can hold braces
Test(logger, text_is_not_a_format)
{
    Logger logger;
    auto out = std::make_shared<std::ostringstream>();
    const std::string fromElsewhere = "cannot read {file}: {0} and {{ }}";

    logger.registerHandler(out);
    logger.setLevel(LoggerLevel::DEBUG);
    logger.error(fromElsewhere);
    logger.warn("half a brace {");
    logger.info("{}");
    logger.ok("braces {{}} with an argument: {}", 7);
    const auto lines = linesOf(out->str());

    AssertEq(lines.size(), 4, "four lines, and nothing thrown");
    AssertStrEq(saidBy(lines[0]).c_str(), fromElsewhere.c_str(), "a message is written as it is");
    AssertStrEq(saidBy(lines[1]).c_str(), "half a brace {", "half a brace too");
    AssertStrEq(saidBy(lines[2]).c_str(), "{}", "a lone {} is not an error");
    AssertStrEq(saidBy(lines[3]).c_str(), "braces {} with an argument: 7", "with arguments, it is a format");
}

Test(logger, level_and_enable)
{
    Logger logger;
    auto out = std::make_shared<std::ostringstream>();

    logger.registerHandler(out);
    logger.setLevel(LoggerLevel::WARN);
    logger.info("hidden");
    logger.warn("shown");
    logger.enable(false);
    logger.error("disabled");
    AssertEq(linesOf(out->str()).size(), 1, "only the warning: %zu lines", linesOf(out->str()).size());
    logger.enable(true);
    logger.error("back");
    AssertEq(linesOf(out->str()).size(), 2, "and back after enable(true)");
}

// Every line arrives whole, none is lost, none is cut by another thread
Test(logger, threads_do_not_mix_lines)
{
    Logger logger;
    auto out = std::make_shared<std::ostringstream>();
    std::vector<std::thread> threads;

    logger.registerHandler(out);
    logger.setLevel(LoggerLevel::DEBUG);
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&logger, t] {
            for (int i = 0; i < LINES; ++i) {
                logger.info("thread {} line {} of {}", t, i, LINES);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto lines = linesOf(out->str());
    std::set<std::string> said;

    AssertEq(lines.size(), THREADS * LINES, "every line is there: %zu", lines.size());
    for (const auto& line : lines) {
        said.insert(saidBy(line));
    }
    AssertEq(said.size(), THREADS * LINES, "each one whole and once: %zu different", said.size());
    for (int t = 0; t < THREADS; ++t) {
        Assert(said.count(std::format("thread {} line {} of {}", t, LINES - 1, LINES)) == 1, "the last line of thread %d", t);
    }
}

// Handlers and the level can change while other threads log
Test(logger, changes_while_logging)
{
    Logger logger;
    auto first = std::make_shared<std::ostringstream>();
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    logger.registerHandler(first);
    logger.setLevel(LoggerLevel::DEBUG);
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&logger, &stop, t] {
            for (int i = 0; !stop; ++i) {
                logger.debug("thread {} line {}", t, i);
            }
        });
    }
    std::vector<std::shared_ptr<std::ostringstream>> later;

    for (int i = 0; i < 20; ++i) {
        later.push_back(std::make_shared<std::ostringstream>());
        logger.registerHandler(later.back());
        logger.setLevel(i % 2 ? LoggerLevel::DEBUG : LoggerLevel::INFO);
        logger.enable(i % 5 != 0);
        std::this_thread::yield();
    }
    stop = true;
    for (auto& thread : threads) {
        thread.join();
    }
    for (const auto& line : linesOf(first->str())) {
        Assert(saidBy(line).rfind("thread ", 0) == 0, "no line is cut: '%s'", line.c_str());
    }
}

namespace
{
    struct Keeper : ILoggerHandler
    {
        std::vector<std::string> lines;

        void log(LoggerLevel, const std::string& message) override { lines.push_back(message); }
    };
}

Test(logger, handlers_come_and_go)
{
    Logger logger;
    auto keeper = std::make_shared<Keeper>();
    const auto id = logger.addHandler(keeper);

    logger.setLevel(LoggerLevel::DEBUG);
    logger.info("one");
    Assert(id != 0 && keeper->lines.size() == 1 && keeper->lines[0] == "one", "a handler of the game gets the lines");
    Assert(logger.removeHandler(id), "it goes");
    logger.info("two");
    AssertEq(keeper->lines.size(), 1u, "and gets no more");
    Assert(!logger.removeHandler(id) && !logger.removeHandler(999), "removing it again, or one that is not there, says no");
    Assert(logger.addHandler(nullptr) == 0, "no handler, no id");
    auto out = std::make_shared<std::ostringstream>();

    logger.registerHandler(out);   // (the old way still works)
    logger.warn("old way");
    AssertStrEq(saidBy(linesOf(out->str()).at(0)).c_str(), "old way", "registerHandler(ostream) still works");
}

Test(logger, ring_keeps_the_last)
{
    Logger logger;
    auto ring = std::make_shared<RingLogHandler>(3);

    logger.addHandler(ring);
    logger.setLevel(LoggerLevel::DEBUG);
    for (int i = 1; i <= 5; ++i) {
        logger.info("line {}", i);
    }
    auto lines = ring->snapshot();

    AssertEq(lines.size(), 3u, "only the last 3");
    AssertStrEq(lines.front().record.text.c_str(), "line 3", "the oldest kept");
    AssertEq(lines.back().id, 5u, "ids count every line ever");
    Assert(lines.front().record.level == LoggerLevel::INFO && !lines.front().record.time.empty(), "with level and time");
    logger.warn("line 6");
    lines = ring->snapshot(5);
    AssertEq(lines.size(), 1u, "since 5: only the new one");
    AssertStrEq(lines[0].record.text.c_str(), "line 6", "it is line 6");
    AssertEq(ring->snapshot(6).size(), 0u, "nothing newer");
    AssertEq(ring->lastId(), 6u, "last id");
}

Test(logger, ring_with_many_threads)
{
    Logger logger;
    auto ring = std::make_shared<RingLogHandler>(100000);
    std::vector<std::thread> threads;

    logger.addHandler(ring);
    logger.setLevel(LoggerLevel::DEBUG);
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&logger, &ring, t] {
            std::uint64_t seen = 0;

            for (int i = 0; i < LINES; ++i) {
                logger.info("thread {} line {}", t, i);
                if (i % 50 == 0) {   // a reader at the same time
                    for (const auto& line : ring->snapshot(seen)) {
                        seen = line.id;
                    }
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto lines = ring->snapshot();
    std::set<std::string> said;

    AssertEq(lines.size(), THREADS * LINES, "every line is there");
    for (const auto& line : lines) {
        said.insert(line.record.text);
    }
    AssertEq(said.size(), THREADS * LINES, "each one whole and once");
}

Test(logger, categories_have_levels)
{
    Logger logger;
    auto out = std::make_shared<std::ostringstream>();
    auto ring = std::make_shared<RingLogHandler>(10);
    auto net = logger.category("net");
    auto audio = logger.category("audio");

    logger.registerHandler(out);
    logger.addHandler(ring);
    logger.setLevel(LoggerLevel::WARN);
    net.debug("hidden: the logger says warn");
    logger.setLevel("net", LoggerLevel::DEBUG);
    net.debug("shown {}", 1);
    audio.debug("hidden: audio follows the logger");
    audio.warn("shown too");
    logger.debug("hidden: the program's own level is warn");
    logger.setLevel("net", LoggerLevel::ERROR);
    net.warn("hidden: net is at error now");
    logger.clearLevel("net");
    net.warn("net follows the logger again");
    const auto lines = ring->snapshot();

    AssertEq(lines.size(), 3u, "three lines got through, got %zu", lines.size());
    Assert(lines[0].record.category == "net" && lines[0].record.text == "shown 1", "the category and the text are apart");
    AssertStrEq(saidBy(linesOf(out->str()).at(0)).c_str(), "[net] shown 1", "a stream shows the category first");
    AssertStrEq(net.name().c_str(), "net", "a category knows its name");
    Assert(logger.category("net").name() == "net", "asking again gives the same one");
    logger.warn("no category");
    AssertStrEq(ring->snapshot().back().record.category.c_str(), "", "the lines of the program have none");
    logger.enable(false);
    audio.error("off");
    AssertEq(ring->snapshot().size(), 4u, "enable(false) silences the categories too");
}

Test(logger, files_are_rotated)
{
    const auto folder = std::filesystem::temp_directory_path() / ("kuge_log_" + std::to_string(::getpid()));
    Logger logger;

    logger.setLevel(LoggerLevel::DEBUG);
    logger.logToFile(folder / "latest.log", 400);
    for (int i = 0; i < 30; ++i) {
        logger.info("a line that takes some room, number {}", i);
    }
    const auto size = [&](const char* name) { return std::filesystem::exists(folder / name) ? std::filesystem::file_size(folder / name) : 0; };

    Assert(size("previous.log") > 0, "the full file became previous.log");
    Assert(size("latest.log") > 0 && size("latest.log") <= 400, "latest.log is under its limit: %zu", static_cast<std::size_t>(size("latest.log")));
    Assert(size("previous.log") <= 400, "so is the old one");
    std::ifstream in(folder / "latest.log");
    std::string last, line;

    while (std::getline(in, line)) {
        last = line;
    }
    Assert(last.find("number 29") != std::string::npos, "the newest line is in latest.log: %s", last.c_str());
    std::filesystem::remove_all(folder);
}
