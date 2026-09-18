#pragma once

#include "../base/AHandler.hpp"
#include <memory>
#include <ostream>

class FileLoggerHandler : public ALoggerHandler {
public:
    FileLoggerHandler(std::shared_ptr<std::ostream> stream) : ALoggerHandler(stream) {};

    void log(LoggerLevel level, const std::string& message) override
    {
        *this->m_streamHandler << level << timeToStr() << " >> " << message << std::endl;
    }
};
