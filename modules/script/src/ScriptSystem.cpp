#include "ScriptSystem.hpp"
#include "Logger.hpp"
#include "Random.hpp"
#include "Time.hpp"
#include <array>
#include <format>

kuge::ScriptSystem::ScriptSystem(std::shared_ptr<ScriptEngine> engine, ScriptSystemOptions options) : m_engine(std::move(engine)), m_options(std::move(options))
{
    m_engine->onPrint([](const std::string& line) { Logger::logger().category("script").info("{}", line); });
}

bool kuge::ScriptSystem::stopped(void) const noexcept
{
    return m_idle;
}

void kuge::ScriptSystem::restart(void) noexcept
{
    m_stopped.clear();
    m_idle = false;
}

bool kuge::ScriptSystem::handle(kw::World& world)
{
    auto log = Logger::logger().category("script");
    Time time;

    try {
        time = world.getResource<Time>();
    } catch (const std::exception&) {
        // a World with no clock (a test): the defaults of Time
    }
    if (m_options.reloadEvery != 0 && time.tick % m_options.reloadEvery == 0) {
        const ScriptEngine::Reload reload = m_engine->reloadChanged();

        if (reload.reloaded) {
            log.info("the scripts were reloaded");
            restart();
        } else if (!reload.error.empty()) {
            log.error("the scripts were not reloaded, the old ones go on: {}", reload.error);
        }
    }
    if (m_idle) {
        return true;
    }
    const std::vector<std::string> scopes = m_engine->scopes(m_options.function);

    if (scopes.empty()) {
        log.error("a script is stopped, the game goes on: there is no function '{}'", m_options.function);
        m_idle = true;
        return true;
    }
    m_engine->seed(seedFor(0, static_cast<std::uint32_t>(time.tick), m_options.seedSalt));
    const std::array<double, 1> args{time.dt};
    std::size_t running = 0;

    // Each scope (each mod) alone: one that fails is stopped, the others go on
    for (const std::string& scope : scopes) {
        if (m_stopped.count(scope)) {
            continue;
        }
        if (const auto result = m_engine->call(world, scope, m_options.function, args); !result.ok) {
            m_stopped.insert(scope);
            log.error("the script{} is stopped, the game goes on: {}", scope.empty() ? "" : std::format(" of '{}'", scope), result.message);
        } else {
            ++running;
        }
    }
    m_idle = running == 0;   // every scope that has the function is stopped
    return true;
}
