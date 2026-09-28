extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Logger.hpp"
#include <atomic>
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
