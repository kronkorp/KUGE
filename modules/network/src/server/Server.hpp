#pragma once

extern "C" {
    #include "kronknet/callback/callback.h"
    #include "kronknet/macros/types.h"
}

namespace kuge::Network
{

    class Server
    {
        public:
            Server(knPort port, knFlags flag);
            ~Server();

        private:
            knServer *m_server;
    };

}
