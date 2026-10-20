#pragma once

#include "../base/IHandler.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>

////////////////////////////////////////////////////////////////////////////
/**
 * @brief  A log file that does not grow for ever: over `maxBytes` it becomes `previous.log` (next to it, replacing the
 *         one before) and a new file starts
 *
 * So there are at most two files, `latest.log` and `previous.log`, about `maxBytes` each. The file is appended to.
 */
////////////////////////////////////////////////////////////////////////////
class RotatingFileHandler : public ILoggerHandler
{
public:
    RotatingFileHandler(std::filesystem::path path, std::uintmax_t maxBytes);

    void log(LoggerLevel level, const std::string& message) override { handle({level, timeToStr(), {}, message}); }
    void handle(const LogRecord& record) override;

private:
    void open(void);

    std::filesystem::path m_path;
    std::uintmax_t        m_maxBytes;
    std::uintmax_t        m_size = 0;
    std::ofstream         m_out;
};
