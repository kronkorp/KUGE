// A chat client in the terminal: joins a room of the chat server, then sends what you type.
//
//     kuge_chat_client <name> [host] [port]        (127.0.0.1:4242 by default)
//     kuge_chat_client Ana --say "hello" --wait 2  (says it once and stays 2 seconds: for scripts)

#include "Chat.hpp"
#include "Matchmaking.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>

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
            pollfd input{STDIN_FILENO, POLLIN, 0};

            if (wait < 0 && poll(&input, 1, 0) > 0) {
                std::string line;

                if (!std::getline(std::cin, line)) {
                    break;
                }
                matchmaking.room()->send(kuge::net::CLIENT_CONNECTION, ChatSay{line});
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
