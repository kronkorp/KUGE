#pragma once
#include <string>
#include "../../LoggerLevel.hpp"

class ILoggerHandler
{
public:
    virtual ~ILoggerHandler() = default;

    virtual void log(LoggerLevel level, const std::string& message) = 0;
};
