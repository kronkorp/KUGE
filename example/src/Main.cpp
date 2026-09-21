#include "Engine.hpp"
#include "Logger.hpp"
#include "Stage.hpp"

namespace
{
    // Logs once per second, and stops the engine after a given number of ticks
    class Ticker : public kw::ISystem
    {
        public:
            Ticker(kuge::Engine& engine, std::uint64_t lastTick)
                : m_engine(engine), m_lastTick(lastTick) {}

            bool handle(kw::World& world) override
            {
                const auto& time = world.getResource<kuge::Time>();

                if (time.tick % time.tickRate == 0) {
                    Logger::logger().info("second {} (tick {})", time.tick / time.tickRate, time.tick);
                }
                if (time.tick >= m_lastTick) {
                    m_engine.stop();
                }
                return true;
            }

        private:
            kuge::Engine& m_engine;
            std::uint64_t m_lastTick;
    };

    class Hello : public kuge::Scene
    {
        public:
            explicit Hello(std::uint64_t seconds) : m_seconds(seconds) {}

            void onEnter(void) override
            {
                const auto ticks = m_seconds * ctx().time().tickRate;

                addSystem(kw::Schedule::Fixed, kuge::stage::Simulation,
                    std::make_unique<Ticker>(ctx().engine(), ticks));
            }

            void onExit(void) override
            {
                Logger::logger().info("Hello left after {} ticks", ctx().time().tick);
            }

        private:
            std::uint64_t m_seconds;
    };
}

// Runs a scene for 3 seconds, headless. Ctrl+C ends it early, and cleanly.
int main()
{
    kuge::Engine engine({.mode = kuge::Engine::Mode::Headless, .tickRate = 60});

    engine.scenes().change<Hello>(3);
    return engine.run();
}
