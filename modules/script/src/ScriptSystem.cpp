#include "ScriptSystem.hpp"
#include "Logger.hpp"
#include "Random.hpp"
#include "Time.hpp"
#include <array>

kuge::ScriptSystem::ScriptSystem(std::shared_ptr<ScriptEngine> engine, ScriptSystemOptions options) : m_engine(std::move(engine)), m_options(std::move(options))
{
    m_engine->onPrint([](const std::string& line) { Logger::logger().category("script").info("{}", line); });
}

bool kuge::ScriptSystem::handle(kw::World& world)
{
    Time time;

    try {
        time = world.getResource<Time>();
    } catch (const std::exception&) {
        // a World with no clock (a test): the defaults of Time
    }
    if (m_options.reloadEvery != 0 && time.tick % m_options.reloadEvery == 0) {
        const ScriptEngine::Reload reload = m_engine->reloadChanged();

        if (reload.reloaded) {
            Logger::logger().category("script").info("the scripts were reloaded");
            m_stopped = false;
        } else if (!reload.error.empty()) {
            Logger::logger().category("script").error("the scripts were not reloaded, the old ones go on: {}", reload.error);
        }
    }
    if (m_stopped) {
        return true;
    }
    m_engine->seed(seedFor(0, static_cast<std::uint32_t>(time.tick), m_options.seedSalt));
    const std::array<double, 1> args{time.dt};

    if (const auto result = m_engine->call(world, m_options.function, args); !result.ok) {
        m_stopped = true;
        Logger::logger().category("script").error("a script is stopped, the game goes on: {}", result.message);
    }
    return true;
}
