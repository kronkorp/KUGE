extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Logger.hpp"
#include <atomic>
#include <thread>
#include <vector>

// NOTE: This one has its executable: the logger of the process is made on its
// first use, and this must be the first, from several threads at once.

Test(logger_init, first_use_from_threads)
{
    constexpr int THREADS = 16;
    std::vector<Logger*> seen(THREADS, nullptr);
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&seen, &go, t] {
            while (!go) {}
            seen[t] = &Logger::logger();
            Logger::logger().info("thread {} is there", t);
        });
    }
    go = true;
    for (auto& thread : threads) {
        thread.join();
    }
    for (int t = 1; t < THREADS; ++t) {
        Assert(seen[t] == seen[0], "thread %d got another logger", t);
    }
}
