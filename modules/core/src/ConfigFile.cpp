#include "ConfigFile.hpp"
#include "Serializer.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>

namespace
{
    constexpr std::string_view SPACES = " \t\r\n";

    std::string_view trim(std::string_view text)
    {
        const auto begin = text.find_first_not_of(SPACES);

        if (begin == std::string_view::npos) {
            return {};
        }
        return text.substr(begin, text.find_last_not_of(SPACES) - begin + 1);
    }

    bool hasSpace(std::string_view text)
    {
        return text.find_first_of(SPACES) != std::string_view::npos;
    }

    template<typename T>
    std::optional<T> parseNumber(const std::optional<std::string>& text)
    {
        T value{};

        if (!text) {
            return std::nullopt;
        }
        const char* end = text->data() + text->size();
        const auto result = std::from_chars(text->data(), end, value);

        if (result.ec != std::errc() || result.ptr != end) {
            return std::nullopt;
        }
        return value;
    }

    std::string lower(std::string_view text)
    {
        std::string out(text);

        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }
}

void kuge::ConfigFile::parse(std::string_view text)
{
    ConfigFile parsed;   // merged at the end: a file with an error adds nothing
    std::string section;
    std::size_t lineNumber = 0;

    while (!text.empty()) {
        const auto newline = text.find('\n');
        std::string_view line = trim(text.substr(0, newline));

        text = (newline == std::string_view::npos) ? std::string_view{} : text.substr(newline + 1);
        ++lineNumber;
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }
        if (line.front() == '[') {
            if (line.back() != ']') {
                throw ConfigError(std::format("line {}: '[' without ']'", lineNumber));
            }
            section = std::string(trim(line.substr(1, line.size() - 2)));
            if (section.empty() || hasSpace(section)) {
                throw ConfigError(std::format("line {}: invalid section name", lineNumber));
            }
            continue;
        }
        const auto equal = line.find('=');

        if (equal == std::string_view::npos) {
            throw ConfigError(std::format("line {}: expected 'key = value'", lineNumber));
        }
        const std::string_view key = trim(line.substr(0, equal));

        if (key.empty() || hasSpace(key)) {
            throw ConfigError(std::format("line {}: invalid key", lineNumber));
        }
        parsed.set(section.empty() ? std::string(key) : std::format("{}.{}", section, key),
            trim(line.substr(equal + 1)));
    }
    for (auto& [key, value] : parsed.m_entries) {
        m_entries[key] = std::move(value);
    }
}

bool kuge::ConfigFile::load(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::stringstream content;

    if (!in) {
        return false;
    }
    content << in.rdbuf();
    parse(content.str());
    return true;
}

void kuge::ConfigFile::save(const std::filesystem::path& path) const
{
    const std::string text = toString();

    try {
        writeFile(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    } catch (const SerializerError& e) {
        throw ConfigError(e.what());
    }
}

std::string kuge::ConfigFile::toString(void) const
{
    std::string text;

    for (const auto& [key, value] : m_entries) {
        text += std::format("{} = {}\n", key, value);
    }
    return text;
}

bool kuge::ConfigFile::has(std::string_view key) const
{
    return m_entries.find(key) != m_entries.end();
}

std::optional<std::string> kuge::ConfigFile::get(std::string_view key) const
{
    const auto it = m_entries.find(key);

    if (it == m_entries.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string kuge::ConfigFile::getString(std::string_view key, std::string_view fallback) const
{
    return get(key).value_or(std::string(fallback));
}

long long kuge::ConfigFile::getInt(std::string_view key, long long fallback) const
{
    return parseNumber<long long>(get(key)).value_or(fallback);
}

double kuge::ConfigFile::getDouble(std::string_view key, double fallback) const
{
    return parseNumber<double>(get(key)).value_or(fallback);
}

bool kuge::ConfigFile::getBool(std::string_view key, bool fallback) const
{
    const auto value = get(key);

    if (!value) {
        return fallback;
    }
    const std::string word = lower(*value);

    if (word == "true" || word == "yes" || word == "on" || word == "1") {
        return true;
    }
    if (word == "false" || word == "no" || word == "off" || word == "0") {
        return false;
    }
    return fallback;
}

void kuge::ConfigFile::set(std::string_view key, std::string_view value)
{
    if (key.empty() || hasSpace(key) || key.find('=') != std::string_view::npos
        || key.front() == '#' || key.front() == ';' || key.front() == '[') {
        throw ConfigError(std::format("invalid key '{}'", key));
    }
    if (value.find_first_of("\r\n") != std::string_view::npos) {
        throw ConfigError(std::format("the value of '{}' has a line break", key));
    }
    m_entries[std::string(key)] = std::string(trim(value));
}

bool kuge::ConfigFile::remove(std::string_view key)
{
    const auto it = m_entries.find(key);

    if (it == m_entries.end()) {
        return false;
    }
    m_entries.erase(it);
    return true;
}
