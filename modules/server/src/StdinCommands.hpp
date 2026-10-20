#pragma once

#include "Commands.hpp"
#include <istream>
#include <memory>

namespace kuge
{

    //! Reads lines from a stream (std::cin for a server) on a thread of its own and queues them as commands, until the
    //! stream ends. The thread is detached and holds the queue, so it can outlive the caller; a command that changes the world
    //! must run in the tick: give the queue to installCommandQueue().
    //!
    //!     auto queue = std::make_shared<kuge::CommandQueue>();
    //!     kuge::readCommandsFrom(std::cin, queue);
    //!
    //! @param in  Must live as long as the thread reads it (std::cin does)
    void readCommandsFrom(std::istream& in, std::shared_ptr<CommandQueue> queue);

}
