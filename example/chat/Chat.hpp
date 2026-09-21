#pragma once

#include "Wire.hpp"
#include <cstdint>
#include <string>

// What the chat says, on the wire. The client and the server both include this.

struct ChatSay
{
    KUGE_MESSAGE(ChatSay, text)
    std::string text;
};

struct ChatLine
{
    KUGE_MESSAGE(ChatLine, from, text)
    std::uint32_t from = 0;      //!< The network id of who said it
    std::string   text;
};

struct ChatJoined
{
    KUGE_MESSAGE(ChatJoined, networkId, name)
    std::uint32_t networkId = 0;
    std::string   name;
};
