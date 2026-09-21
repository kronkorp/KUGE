#pragma once

#include "input/Event.hpp"
#include <vector>

namespace kuge
{

    //! Where the inputs and the window events come from. A backend gives one.
    class IInputSource
    {
        public:
            virtual ~IInputSource(void) = default;

            //! Adds to out what happened since the last call, oldest first
            virtual void poll(std::vector<Event>& out) = 0;
    };

}
