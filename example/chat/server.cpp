// A chat server with no window: a lobby, and rooms of four that repeat what is said in them.
//
//     kuge_chat_server [port]        (4242 by default; the rooms take the ports after it)
//
// Ctrl+C stops it: every room is closed properly.

#include "Chat.hpp"
#include "GameServer.hpp"
#include "Logger.hpp"
#include <cstdlib>

class ChatRoom : public kuge::server::RoomScene
{
    public:
        using RoomScene::RoomScene;

    protected:
        void onRoomEnter(void) override
        {
            on<ChatSay>([this](const Player& who, const ChatSay& say) {
                broadcast(ChatLine{who.networkId, say.text});
            });
        }

        void onPlayerJoined(const Player& player) override
        {
            Logger::logger().info("room {}: {} joined as #{}", init().roomId, player.name, player.networkId);
            broadcast(ChatJoined{player.networkId, player.name});
        }

        void onPlayerLeft(const Player& player, kuge::net::DisconnectReason) override
        {
            Logger::logger().info("room {}: {} left", init().roomId, player.name);
        }
};

int main(int argc, char** argv)
{
    kuge::server::ServerConfig config;

    if (argc > 1) {
        config.lobbyPort = static_cast<std::uint16_t>(std::atoi(argv[1]));
        config.roomPortFirst = static_cast<std::uint16_t>(config.lobbyPort + 1);
    }
    kuge::server::GameServer server(config);

    server.addRoomType<ChatRoom>("chat", {.maxPlayers = 4, .idleTimeout = 60.0});
    return server.run();
}
