
#pragma once

#include "kronkworld/Kronkworld.hpp"

namespace kuge
{

    class Engine
    {
        public:
            Engine();
            ~Engine();

        private:
            kw::World world;
    };

}
