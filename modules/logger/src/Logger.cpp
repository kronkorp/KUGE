#include "Logger.hpp"
#include "LoggerLevel.hpp"
#include "handler/impl/FileHandler.hpp"
#include "handler/impl/TtyHandler.hpp"
#include <iostream>
#include <memory>
#include <ostream>
#include <vector>
#include <ctime>
#ifdef _WIN32
    #include <io.h>
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace
{
    // Is this output a terminal that shows colours? Windows' console shows them once asked to
    // (Windows 10 and later): when it cannot be asked, there are no colours.
    bool colourTerminal(bool error)
    {
#ifdef _WIN32
        const HANDLE console = ::GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        DWORD mode = 0;

        return ::_isatty(error ? 2 : 1) && ::GetConsoleMode(console, &mode) &&
            ::SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
        return ::isatty(error ? STDERR_FILENO : STDOUT_FILENO);
#endif
    }
}

Logger::Logger() {}

Logger::~Logger() {}

Logger::Logger(std::shared_ptr<std::ostream> handler)
{
    this->registerHandler(handler);
}

std::shared_ptr<Logger> Logger::makeDefault(void)
{
    auto logger = std::make_shared<Logger>();

    logger->registerHandler(std::make_shared<std::ofstream>("latest.log", std::ios::app));
    logger->registerHandler(std::make_shared<std::ostream>(std::cout.rdbuf()));
    logger->setLevel(LoggerLevel::DEBUG);
    return logger;
}

void Logger::registerHandler(std::shared_ptr<std::ostream> handler)
{
    std::lock_guard lock(this->m_mutex);

    if ((handler->rdbuf() == std::cout.rdbuf() && colourTerminal(false)) ||
        (handler->rdbuf() == std::cerr.rdbuf() && colourTerminal(true))) {
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

void Logger::log(LoggerLevel level, std::string_view text)
{
    if (level < this->m_currentLevel || !this->enable()) return;
    this->write(level, std::string(text));
}

void Logger::write(LoggerLevel level, const std::string& text)
{
    std::lock_guard lock(this->m_mutex);

    for (auto& handler : this->m_handlers) {
        handler->log(level, text);
    }
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