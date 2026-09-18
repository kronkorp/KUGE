#pragma once

#include "IHandler.hpp"
#include <memory>
#include <ostream>

class ALoggerHandler : public ILoggerHandler {
public:
    ALoggerHandler(std::shared_ptr<std::ostream> stream) : m_streamHandler(stream) {};
    ~ALoggerHandler() = default;

protected:
    std::shared_ptr<std::ostream> m_streamHandler;
};
