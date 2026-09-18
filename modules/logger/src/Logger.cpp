#include "Logger.hpp"
#include "LoggerLevel.hpp"
#include "handler/impl/FileHandler.hpp"
#include "handler/impl/TtyHandler.hpp"
#include <iostream>
#include <memory>
#include <ostream>
#include <vector>
#include <unistd.h>
#include <ctime>

Logger::Logger() {}

Logger::~Logger() {}

Logger::Logger(std::shared_ptr<std::ostream> handler)
{
    this->registerHandler(handler);
}

void Logger::registerHandler(std::shared_ptr<std::ostream> handler)
{
    if ((handler->rdbuf() == std::cout.rdbuf() && isatty(STDOUT_FILENO)) ||
        (handler->rdbuf() == std::cerr.rdbuf() && isatty(STDERR_FILENO))) {
            this->m_handlers.push_back(std::make_unique<TtyLoggerHandler>(handler));
    } else {
            this->m_handlers.push_back(std::make_unique<FileLoggerHandler>(handler));
    }
}

void Logger::debug(std::string_view format)
{
    this->log(LoggerLevel::DEBUG, format);
}

void Logger::info(std::string_view format)
{
    this->log(LoggerLevel::INFO, format);
}

void Logger::ok(std::string_view format)
{
    this->log(LoggerLevel::SUCCESS, format);
}

void Logger::warn(std::string_view format)
{
    this->log(LoggerLevel::WARN, format);
}

void Logger::error(std::string_view format)
{
    this->log(LoggerLevel::ERROR, format);
}

void Logger::setLevel(LoggerLevel level)
{
    this->m_currentLevel = level;
}

bool Logger::enable(void) const
{
    return this->m_enable;
}

void Logger::enable(bool enabled)
{
    this->m_enable = enabled;
}