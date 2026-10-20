#pragma once

#include "LoggerLevel.hpp"
#include "handler/base/IHandler.hpp"
#include "handler/impl/RingHandler.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <iostream>
#include <vector>

class Logger;

////////////////////////////////////////////////////////////////////////////
/**
 * @brief  A part of the program that logs under its own name and its own level (see Logger::category())
 *
 *     static LogCategory net = Logger::logger().category("net");
 *     net.warn("bad packet from {}", peer);
 *     Logger::logger().setLevel("net", LoggerLevel::DEBUG);      // only the net talks
 *
 * It is a small handle: keep it or ask for it again. A category with no level of its own follows the level of the
 * logger. It lives as long as the logger.
 */
////////////////////////////////////////////////////////////////////////////
class LogCategory
{
public:
    template<typename ...Args>
    void debug(std::string_view format, Args&&... args) { this->log(LoggerLevel::DEBUG, format, args...); }
    template<typename ...Args>
    void info(std::string_view format, Args&&... args) { this->log(LoggerLevel::INFO, format, args...); }
    template<typename ...Args>
    void ok(std::string_view format, Args&&... args) { this->log(LoggerLevel::SUCCESS, format, args...); }
    template<typename ...Args>
    void warn(std::string_view format, Args&&... args) { this->log(LoggerLevel::WARN, format, args...); }
    template<typename ...Args>
    void error(std::string_view format, Args&&... args) { this->log(LoggerLevel::ERROR, format, args...); }

    //! With no argument the text is written as it is (it is not a format), as for Logger
    template<typename ...Args>
    void log(LoggerLevel level, std::string_view format, Args&&... args);

    const std::string& name(void) const noexcept { return *m_name; }

private:
    friend class Logger;

    LogCategory(Logger& logger, const std::string& name, const std::atomic<int>& level) : m_logger(&logger), m_name(&name), m_level(&level) {}

    Logger*                   m_logger;
    const std::string*        m_name;
    const std::atomic<int>*   m_level;   //!< Its own level, or -1 to follow the logger's
};

class Logger {
public:
    Logger();
    Logger(std::shared_ptr<std::ostream> handler);
    ~Logger();

    bool enable(void) const;
    void enable(bool enabled);

    //! The logger of the process, made on first use. Thread-safe: any thread may
    //! call it, and log through it (a line is never cut by another one).
    static Logger &logger(void)
    {
        static const std::shared_ptr<Logger> instance = makeDefault();

        return *instance;
    }

    void debug(std::string_view format);
    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief         Print a "debug" level message on the current stream
     *
     * @param format  The format of the string to write
     * @param args    The arguments to give to the "format" string
     * @return        This method returns nothing
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename ...Args>
    void debug(std::string_view format, Args&&... args)
    {
        this->log(LoggerLevel::DEBUG, format, std::forward<Args>(args)...);
    }
    ////////////////////////////////////////////////////////////////////////////


    void info(std::string_view format);
    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief         Print a "info" level message on the current stream
     *
     * @param format  The format of the string to write
     * @param args    The arguments to give to the "format" string
     * @return        This method returns nothing
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename ...Args>
    void info(std::string_view format, Args&&... args)
    {
        this->log(LoggerLevel::INFO, format, std::forward<Args>(args)...);
    }
    ////////////////////////////////////////////////////////////////////////////


    void ok(std::string_view format);
    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief         Print a "ok" level message on the current stream
     *
     * @param format  The format of the string to write
     * @param args    The arguments to give to the "format" string
     * @return        This method returns nothing
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename ...Args>
    void ok(std::string_view format, Args&&... args)
    {
        this->log(LoggerLevel::SUCCESS, format, std::forward<Args>(args)...);
    }
    ////////////////////////////////////////////////////////////////////////////


    void warn(std::string_view format);
    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief         Print a "warning" level message on the current stream
     *
     * @param format  The format of the string to write
     * @param args    The arguments to give to the "format" string
     * @return        This method returns nothing
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename ...Args>
    void warn(std::string_view format, Args&&... args)
    {
        this->log(LoggerLevel::WARN, format, std::forward<Args>(args)...);
    }
    ////////////////////////////////////////////////////////////////////////////


    void error(std::string_view format);
    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief         Print a "error" level message on the current stream
     *
     * @param format  The format of the string to write
     * @param args    The arguments to give to the "format" string
     * @return        This method returns nothing
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename ...Args>
    void error(std::string_view format, Args&&... args)
    {
        this->log(LoggerLevel::ERROR, format, std::forward<Args>(args)...);
    }
    ////////////////////////////////////////////////////////////////////////////


    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief         Log a message on the currents streams
     *
     * @param level   The level of logging (warning, ok, info...)
     * @param format  The format of the string to write
     * @param args    The arguments to give to the "format" string
     * @return        This method returns nothing
     */
    ////////////////////////////////////////////////////////////////////////////
    template<typename ...Args>
    void log(LoggerLevel level, std::string_view format, Args&&... args)
    {
        if (level < this->m_currentLevel || !this->enable()) return;
        this->write(level, std::vformat(format, std::make_format_args(args...)));
    }
    ////////////////////////////////////////////////////////////////////////////


    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief         Log a message as it is, with no arguments: it is text, not a format
     *
     * A message that comes from elsewhere (an exception, a file, a player) can hold
     * braces: it must not be read as a format. This is what debug(), info(), ok(),
     * warn() and error() do when they are given no argument (so "{{" stays "{{").
     *
     * @param level   The level of logging (warning, ok, info...)
     * @param text    The message, written as it is
     */
    ////////////////////////////////////////////////////////////////////////////
    void log(LoggerLevel level, std::string_view text);
    ////////////////////////////////////////////////////////////////////////////


    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief           Register an handler to log in
     *
     * @param handler  The handler to register
     * @return         This method returns nothing
     */
    ////////////////////////////////////////////////////////////////////////////
    void registerHandler(std::shared_ptr<std::ostream> handler);
    ////////////////////////////////////////////////////////////////////////////

    using HandlerId = std::uint64_t;   //!< 0 is no handler

    //! Adds somewhere for the lines to go: a RingLogHandler for a panel of the game, a file, your own ILoggerHandler.
    //! It is called with the mutex of the logger held and from any thread (see ILoggerHandler).
    HandlerId addHandler(std::shared_ptr<ILoggerHandler> handler);

    //! Takes it away (a scene that listens stops when it leaves). @return  false if there is no such handler
    bool removeHandler(HandlerId id);

    //! A file that does not grow for ever: over `maxBytes` it becomes `previous.log` next to it (see RotatingFileHandler)
    HandlerId logToFile(const std::filesystem::path& path, std::uintmax_t maxBytes = 8u * 1024 * 1024);

    //! A name for the lines of a part of the program, with a level of its own (see LogCategory)
    LogCategory category(std::string_view name);

    //! The level of a category: below it the category says nothing, whatever the level of the logger
    void setLevel(std::string_view category, LoggerLevel level);

    //! The category follows the level of the logger again
    void clearLevel(std::string_view category);

    //! The words of a list ("net,physics", for `--debug net,physics`): those categories go to the debug level, the others stay as
    //! they are, instead of turning every message on.
    void debugCategories(std::string_view list);


    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief           Register an handler to log in
     *
     * @param handler  The handler to register
     * @return         This method returns nothing
     */
    ////////////////////////////////////////////////////////////////////////////
    void setLevel(LoggerLevel level);
    ////////////////////////////////////////////////////////////////////////////

private:
    friend class LogCategory;

    static std::shared_ptr<Logger> makeDefault(void);

    void write(LoggerLevel level, const std::string& text);   //!< To every handler (the level was checked)
    void write(LoggerLevel level, const std::string& category, const std::string& text);
    bool enabledFor(const std::atomic<int>& own, LoggerLevel level) const
    {
        const int mine = own.load();

        return this->enable() && static_cast<int>(level) >= (mine >= 0 ? mine : static_cast<int>(this->m_currentLevel.load()));
    }

    struct Slot
    {
        std::string      name;
        std::atomic<int> level{-1};
    };
    Slot& slotOf(std::string_view name);   //!< (the mutex is held)

    std::mutex                                   m_mutex;                             //!< Held while the handlers write: they are not thread-safe
    std::vector<std::pair<HandlerId, std::shared_ptr<ILoggerHandler>>> m_handlers;    //!< The handlers (the streams)
    HandlerId                                    m_lastHandler = 0;
    std::map<std::string, std::unique_ptr<Slot>, std::less<>> m_categories;           //!< Stable: a LogCategory points into it
    std::atomic<LoggerLevel>                     m_currentLevel{LoggerLevel::INFO};   //!< The current level of the logger (Debuf, info, ...). All lower level will be ignored
    std::atomic<bool>                            m_enable{true};
};

template<typename ...Args>
void LogCategory::log(LoggerLevel level, std::string_view format, Args&&... args)
{
    if (!m_logger->enabledFor(*m_level, level)) {
        return;
    }
    if constexpr (sizeof...(Args) == 0) {
        m_logger->write(level, *m_name, std::string(format));
    } else {
        m_logger->write(level, *m_name, std::vformat(format, std::make_format_args(args...)));
    }
}
