#include "LoggerLevel.hpp"
#include <ctime>
#include <iomanip>

const char *ColorToLevel(LoggerLevel level)
{
    switch (level) {
        case LoggerLevel::DEBUG:   return "\033[36m";  // Cyan
        case LoggerLevel::INFO:    return "\033[34m";  // Bleu
        case LoggerLevel::SUCCESS: return "\033[32m";  // Vert
        case LoggerLevel::WARN:    return "\033[33m";  // Jaune
        case LoggerLevel::ERROR:   return "\033[31m";  // Rouge
        default:                   return "\033[0m";   // Nothing (default)
    }
};

std::ostream& operator<<(std::ostream& os, LoggerLevel level)
{
    switch (level) {
        case LoggerLevel::DEBUG:   return os << "[ DEBUG ] ";
        case LoggerLevel::INFO:    return os << "[ INFO  ] ";
        case LoggerLevel::SUCCESS: return os << "[SUCCESS] ";
        case LoggerLevel::WARN:    return os << "[WARNING] ";
        case LoggerLevel::ERROR:   return os << "[ ERROR ] ";
        default:                   return os << "[UNKNOWN] ";
    }
}

std::string timeToStr(void)
{
    const time_t time = std::time(nullptr);
    std::tm* localTime = std::localtime(&time);
    std::stringstream ss;
    ss << std::put_time(localTime, "%Y-%m-%d %H:%M:%S"); 
    return ss.str();
}
