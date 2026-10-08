// A chat client in the terminal: joins a room of the chat server, then sends what you type.
//
//     kuge_chat_client <name> [host] [port]        (127.0.0.1:4242 by default)
//     kuge_chat_client Ana --say "hello" --wait 2  (says it once and stays 2 seconds: for scripts)

#include "Chat.hpp"
#include "Matchmaking.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace
{
    // What is typed, read by a thread of its own: a line is only there once Enter is pressed,
    // and the network is polled meanwhile
    struct Typed
    {
        std::mutex              mutex;
        std::deque<std::string> lines;
        bool                    closed = false;   //!< No more lines (Ctrl+D, or Ctrl+Z on Windows)
    };

    std::shared_ptr<Typed> readTyped(void)
    {
        auto typed = std::make_shared<Typed>();

        // Detached: when the chat ends, it may still wait for a line, and the process ends it
        std::thread([typed] {
            std::string line;

            while (std::getline(std::cin, line)) {
                std::lock_guard lock(typed->mutex);

                typed->lines.push_back(line);
            }
            std::lock_guard lock(typed->mutex);

            typed->closed = true;
        }).detach();
        return typed;
    }
}

int main(int argc, char** argv)
{
    std::string name = argc > 1 ? argv[1] : "player";
    std::string host = "127.0.0.1";
    std::uint16_t port = 4242;
    std::string say;
    double wait = -1.0;

    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--say") && i + 1 < argc) {
            say = argv[++i];
        } else if (!std::strcmp(argv[i], "--wait") && i + 1 < argc) {
            wait = std::atof(argv[++i]);
        } else if (i == 2) {
            host = argv[i];
        } else {
            port = static_cast<std::uint16_t>(std::atoi(argv[i]));
        }
    }
    kuge::net::Net net;
    kuge::net::MatchmakingClient matchmaking(net);
    bool over = false;
    bool said = false;
    const auto start = std::chrono::steady_clock::now();
    const auto typed = wait < 0 ? readTyped() : nullptr;

    matchmaking.onJoined([&](kuge::net::Endpoint& room, const kuge::net::Welcome& welcome) {
        std::cout << "* in room " << welcome.roomId << " as #" << welcome.networkId << std::endl;
        room.on<ChatJoined>([](kuge::net::ConnectionId, const ChatJoined& joined) { std::cout << "* " << joined.name << " joined (#" << joined.networkId << ")" << std::endl; });
        room.on<ChatLine>([](kuge::net::ConnectionId, const ChatLine& line) { std::cout << "#" << line.from << ": " << line.text << std::endl; });
    });
    matchmaking.onRoomClosed([&](kuge::net::RoomEnd) { std::cout << "* the room closed" << std::endl; over = true; });
    matchmaking.onFailed([&](const std::string& why) { std::cout << "* " << why << std::endl; over = true; });
    matchmaking.connectLobby(kuge::net::Protocol::Tcp, host, port);
    matchmaking.join("chat", name);
    while (!over) {
        net.poll();
        if (matchmaking.state() == kuge::net::MatchmakingClient::State::InRoom) {
            if (!say.empty() && !said) {
                said = true;
                matchmaking.room()->send(kuge::net::CLIENT_CONNECTION, ChatSay{say});
            }
            if (typed) {
                std::lock_guard lock(typed->mutex);

                for (; !typed->lines.empty(); typed->lines.pop_front()) {
                    matchmaking.room()->send(kuge::net::CLIENT_CONNECTION, ChatSay{typed->lines.front()});
                }
                if (typed->closed) {
                    break;
                }
            }
        }
        if (wait >= 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > wait) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    matchmaking.disconnect();
    return 0;
}
