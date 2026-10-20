#pragma once

#include "../base/AHandler.hpp"
#include <memory>
#include <ostream>

class TtyLoggerHandler : public ALoggerHandler {
public:
    TtyLoggerHandler(std::shared_ptr<std::ostream> stream) : ALoggerHandler(stream) {};

    void log(LoggerLevel level, const std::string& message) override
    {
        *this->m_streamHandler << ColorToLevel(level) << level  << "\033[0m" << timeToStr() << " >> " << message << std::endl;
    }

    void handle(const LogRecord& record) override
    {
        *this->m_streamHandler << ColorToLevel(record.level) << record.level << "\033[0m" << record.time << " >> "
                               << lineText(record) << std::endl;
    }
};
