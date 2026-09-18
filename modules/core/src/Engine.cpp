#include "Engine.hpp"
#include "Logger.hpp"
#include "LoggerLevel.hpp"

kuge::Engine::Engine()
{
    Logger::logger().info("Engine constructed");
}

kuge::Engine::~Engine()
{
    Logger::logger().info("Engine destroyed");
}
