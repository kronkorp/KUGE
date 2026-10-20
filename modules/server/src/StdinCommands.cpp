#include "StdinCommands.hpp"
#include <string>
#include <thread>

void kuge::readCommandsFrom(std::istream& in, std::shared_ptr<CommandQueue> queue)
{
    std::thread([&in, queue = std::move(queue)] {
        std::string line;

        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                queue->push(line);
            }
        }
    }).detach();
}
