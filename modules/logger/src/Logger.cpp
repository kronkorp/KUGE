#include "Logger.hpp"
#include "LoggerLevel.hpp"
#include "handler/impl/FileHandler.hpp"
#include "handler/impl/RotatingFileHandler.hpp"
#include "handler/impl/TtyHandler.hpp"
#include <iostream>
#include <memory>
#include <ostream>
#include <vector>
#include <algorithm>
#include <ctime>
#include <fstream>
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

std::string lineText(const LogRecord& record)
{
    return record.category.empty() ? record.text : "[" + record.category + "] " + record.text;
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

    logger->logToFile("latest.log");
    logger->registerHandler(std::make_shared<std::ostream>(std::cout.rdbuf()));
    logger->setLevel(LoggerLevel::DEBUG);
    return logger;
}

void Logger::registerHandler(std::shared_ptr<std::ostream> handler)
{
    std::lock_guard lock(this->m_mutex);

    if ((handler->rdbuf() == std::cout.rdbuf() && colourTerminal(false)) ||
        (handler->rdbuf() == std::cerr.rdbuf() && colourTerminal(true))) {
            this->m_handlers.emplace_back(++this->m_lastHandler, std::make_shared<TtyLoggerHandler>(handler));
    } else {
            this->m_handlers.emplace_back(++this->m_lastHandler, std::make_shared<FileLoggerHandler>(handler));
    }
}

Logger::HandlerId Logger::addHandler(std::shared_ptr<ILoggerHandler> handler)
{
    if (!handler) {
        return 0;
    }
    std::lock_guard lock(this->m_mutex);

    this->m_handlers.emplace_back(++this->m_lastHandler, std::move(handler));
    return this->m_lastHandler;
}

bool Logger::removeHandler(HandlerId id)
{
    std::lock_guard lock(this->m_mutex);
    const auto found = std::find_if(this->m_handlers.begin(), this->m_handlers.end(), [id](const auto& item) { return item.first == id; });

    if (found == this->m_handlers.end()) {
        return false;
    }
    this->m_handlers.erase(found);
    return true;
}

Logger::HandlerId Logger::logToFile(const std::filesystem::path& path, std::uintmax_t maxBytes)
{
    return this->addHandler(std::make_shared<RotatingFileHandler>(path, maxBytes));
}

Logger::Slot& Logger::slotOf(std::string_view name)
{
    auto found = this->m_categories.find(name);

    if (found == this->m_categories.end()) {
        found = this->m_categories.emplace(std::string(name), std::make_unique<Slot>()).first;
        found->second->name = std::string(name);
    }
    return *found->second;
}

LogCategory Logger::category(std::string_view name)
{
    std::lock_guard lock(this->m_mutex);
    Slot& slot = this->slotOf(name);

    return LogCategory(*this, slot.name, slot.level);
}

void Logger::setLevel(std::string_view category, LoggerLevel level)
{
    std::lock_guard lock(this->m_mutex);

    this->slotOf(category).level = static_cast<int>(level);
}

void Logger::clearLevel(std::string_view category)
{
    std::lock_guard lock(this->m_mutex);

    this->slotOf(category).level = -1;
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
    this->write(level, std::string(), text);
}

void Logger::write(LoggerLevel level, const std::string& category, const std::string& text)
{
    const LogRecord record{level, timeToStr(), category, text};   // made once: every handler shows the same
    std::lock_guard lock(this->m_mutex);

    for (auto& handler : this->m_handlers) {
        handler.second->handle(record);
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

void Logger::debugCategories(std::string_view list)
{
    while (!list.empty()) {
        const auto comma = list.find(',');
        std::string_view word = list.substr(0, comma);

        while (!word.empty() && word.front() == ' ') {
            word.remove_prefix(1);
        }
        while (!word.empty() && word.back() == ' ') {
            word.remove_suffix(1);
        }
        if (!word.empty()) {
            this->setLevel(word, LoggerLevel::DEBUG);
        }
        list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
    }
}
