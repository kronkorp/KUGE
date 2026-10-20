#include "RotatingFileHandler.hpp"
#include <sstream>
#include <system_error>

RotatingFileHandler::RotatingFileHandler(std::filesystem::path path, std::uintmax_t maxBytes)
    : m_path(std::move(path)), m_maxBytes(maxBytes)
{
    std::error_code ignored;

    if (!m_path.parent_path().empty()) {
        std::filesystem::create_directories(m_path.parent_path(), ignored);
    }
    open();
}

void RotatingFileHandler::open(void)
{
    std::error_code error;

    m_out.open(m_path, std::ios::app);
    m_size = std::filesystem::exists(m_path, error) ? std::filesystem::file_size(m_path, error) : 0;
    if (error) {
        m_size = 0;
    }
}

void RotatingFileHandler::handle(const LogRecord& record)
{
    std::ostringstream line;

    line << record.level << record.time << " >> " << lineText(record) << '\n';
    const std::string text = line.str();

    if (m_maxBytes > 0 && m_size > 0 && m_size + text.size() > m_maxBytes) {
        std::error_code error;

        m_out.close();
        std::filesystem::rename(m_path, m_path.parent_path() / "previous.log", error);   // (replaces the one before)
        if (error) {
            std::filesystem::remove(m_path, error);
        }
        open();
    }
    if (m_out) {
        m_out << text << std::flush;
        m_size += text.size();
    }
}
