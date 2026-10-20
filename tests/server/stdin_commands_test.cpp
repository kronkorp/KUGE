extern "C" {
    #include "kronklab/kronklab.h"
}
#include "StdinCommands.hpp"
#include <chrono>
#include <sstream>
#include <thread>

// NOTE: kronklab test names are limited to 31 characters.

Test(stdin_commands, lines_are_queued)
{
    static std::istringstream in("echo hi\n\nset speed 3\r\nlast");   // (static: the thread may outlive this function)
    auto queue = std::make_shared<kuge::CommandQueue>();
    std::vector<std::string> lines;

    in.clear();
    kuge::readCommandsFrom(in, queue);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (lines.size() < 3 && std::chrono::steady_clock::now() < end) {
        for (auto& line : queue->take()) {
            lines.push_back(std::move(line));
        }
        std::this_thread::yield();
    }
    Assert(lines.size() == 3, "three lines (the empty one is dropped): %zu", lines.size());
    Assert(lines[0] == "echo hi" && lines[1] == "set speed 3" && lines[2] == "last", "in order, without the \\r");
}
