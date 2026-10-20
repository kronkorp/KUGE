#pragma once

#include "Wire.hpp"
#include <cstdint>
#include <string>

namespace kuge::net
{

    //! A client says a line to the room (see RoomScene::enableChat() for what the server does with it)
    struct ChatSend
    {
        KUGE_MESSAGE(ChatSend, text)
        std::string text;
    };

    //! The room says a line to everybody. `from` is the network id of the player (the room's way of naming it); the
    //! text is the one the server cleaned.
    struct ChatLine
    {
        KUGE_MESSAGE(ChatLine, from, text)
        std::uint32_t from = 0;
        std::string   text;
    };

}
