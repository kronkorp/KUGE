#pragma once
#include <cstdint>
#include <string>
#include "../../LoggerLevel.hpp"

//! One line of the log, made once by the logger: every handler shows the same
struct LogRecord
{
    LoggerLevel level = LoggerLevel::INFO;
    std::string time;       //!< "2026-10-20 12:47:10"
    std::string category;   //!< "net", "audio"...; empty for the lines of the whole program
    std::string text;
};

//! "[net] text", or "text" for a line with no category: the part of a line after " >> "
std::string lineText(const LogRecord& record);

////////////////////////////////////////////////////////////////////////////
/**
 * @brief  Where the lines of a Logger go (the terminal, a file, a panel of the game...)
 *
 * A handler is called with the mutex of the logger held and from any thread: it must be quick, must not
 * log (it would wait for itself) and must not call the logger. A game that draws the log in its window
 * keeps the lines in a RingLogHandler and reads them from its own thread.
 */
////////////////////////////////////////////////////////////////////////////
class ILoggerHandler
{
public:
    virtual ~ILoggerHandler() = default;

    //! The simplest way to be a handler: the text of the line, with its category in front
    virtual void log(LoggerLevel level, const std::string& message) = 0;

    //! The whole line. By default it gives log() the text with its category in front.
    virtual void handle(const LogRecord& record) { log(record.level, lineText(record)); }
};
