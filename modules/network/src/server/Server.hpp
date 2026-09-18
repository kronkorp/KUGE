#pragma once

#include <functional>
extern "C" {
    #include "kronknet/callback/callback.h"
    #include "kronknet/macros/types.h"
    #include "kronknet/server/server.h"
}

namespace kuge::Network
{

    class Server
    {
        public:
            Server(knPort port, knFlags flag);
            ~Server();

            void* userPtr(void)      const;
            void  userPtr(void *ptr);

            enum class Callback {
                onRead,
                onDrain,
                onConnection,
                onDisconnection,
            };

            void  setCallback(Callback type);

        private:
            knServer *m_server;
    };

}
