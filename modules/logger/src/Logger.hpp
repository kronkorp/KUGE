#pragma once

#include "LoggerLevel.hpp"
#include "handler/base/IHandler.hpp"
#include <atomic>
#include <endian.h>
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
    static std::shared_ptr<Logger> makeDefault(void);

    void write(LoggerLevel level, const std::string& text);   //!< To every handler (the level was checked)

    std::mutex                                   m_mutex;                             //!< Held while the handlers write: they are not thread-safe
    std::vector<std::unique_ptr<ILoggerHandler>> m_handlers;                          //!< The handlers (the streams)
    std::atomic<LoggerLevel>                     m_currentLevel{LoggerLevel::INFO};   //!< The current level of the logger (Debuf, info, ...). All lower level will be ignored
    std::atomic<bool>                            m_enable{true};
};
