#include "GameServer.hpp"
#include "Lobby.hpp"

namespace kuge::server
{

    GameServer::GameServer(ServerConfig config)
        : m_state(std::make_shared<ServerState>()),
          m_engine(Engine::Config{.mode = Engine::Mode::Headless, .tickRate = config.tickRate, .workers = config.workers})
    {
        m_state->config = std::move(config);
    }

    GameServer::~GameServer(void) = default;

    void GameServer::addRoomType(const std::string& name, RoomTypeConfig config, ServerState::RoomFactory factory)
    {
        if (m_started) {
            throw std::logic_error("GameServer: the room types are added before the server starts");
        }
        m_state->roomTypes[name] = ServerState::RoomType{config, std::move(factory)};
    }

    void GameServer::start(void)
    {
        if (!m_started) {
            m_started = true;
            m_engine.scenes().change<LobbyScene>(m_state);
        }
    }

    int GameServer::run(void)
    {
        start();
        return m_engine.run();
    }

    void GameServer::stop(void) noexcept
    {
        m_engine.stop();
    }

}
