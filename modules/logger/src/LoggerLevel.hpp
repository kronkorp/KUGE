#pragma once

#include <iostream>
#include <string>
#include <ctime>

////////////////////////////////////////////////////////////////////////////
/**
    * @brief  Enum corresponding to all types of log levels
    */
////////////////////////////////////////////////////////////////////////////
enum class LoggerLevel {
    DEBUG,   //!< Lowest type of the logger
    INFO,    //!< Infos (like a element position, an element doing smth...)
    SUCCESS, //!< Ok (like a texture successfully loaded)
    WARN,    //!< Warning (like a missing texture, an interaction failed...)
    ERROR,   //!< Error (somthing necessary went reallyyy wrong)

    Count,    //!< The number of logging levels
};
////////////////////////////////////////////////////////////////////////////

std::string timeToStr(void);
std::ostream& operator<<(std::ostream& os, LoggerLevel level);
const char *ColorToLevel(LoggerLevel level);
