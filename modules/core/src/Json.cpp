#include "Json.hpp"
#include <charconv>
#include <cmath>
#include <fstream>
#include <format>
#include <sstream>

namespace
{
    using kuge::Json;
    using kuge::JsonError;

    constexpr int MAX_DEPTH = 64;

    class Parser
    {
        public:
            explicit Parser(std::string_view text) : m_text(text) {}

            Json run(void)
            {
                Json value = parseValue(0);

                skip();
                if (m_at != m_text.size()) {
                    fail("something after the value");
                }
                return value;
            }

        private:
            [[noreturn]] void fail(const std::string& what) const
            {
                int line = 1, column = 1;

                for (std::size_t i = 0; i < m_at && i < m_text.size(); ++i) {
                    if (m_text[i] == '\n') {
                        ++line;
                        column = 1;
                    } else {
                        ++column;
                    }
                }
                throw JsonError(std::format("line {}, column {}: {}", line, column, what));
            }

            void skip(void)
            {
                while (m_at < m_text.size() && (m_text[m_at] == ' ' || m_text[m_at] == '\t' || m_text[m_at] == '\n' || m_text[m_at] == '\r')) {
                    ++m_at;
                }
            }

            bool take(char c)
            {
                skip();
                if (m_at < m_text.size() && m_text[m_at] == c) {
                    ++m_at;
                    return true;
                }
                return false;
            }

            void expect(char c)
            {
                if (!take(c)) {
                    fail(std::format("expected '{}'", c));
                }
            }

            Json parseValue(int depth)
            {
                if (depth > MAX_DEPTH) {
                    fail("too deep");
                }
                skip();
                if (m_at >= m_text.size()) {
                    fail("the text ends too soon");
                }
                const char c = m_text[m_at];

                if (c == '{') {
                    return parseObject(depth);
                }
                if (c == '[') {
                    return parseArray(depth);
                }
                if (c == '"') {
                    return Json(parseString());
                }
                if (c == 't' || c == 'f' || c == 'n') {
                    return parseWord();
                }
                return parseNumber();
            }

            Json parseWord(void)
            {
                for (const auto& [word, value] : {std::pair<std::string_view, Json>{"true", Json(true)}, {"false", Json(false)}, {"null", Json()}}) {
                    if (m_text.compare(m_at, word.size(), word) == 0) {
                        m_at += word.size();
                        return value;
                    }
                }
                fail("expected true, false or null");
            }

            Json parseNumber(void)
            {
                const std::size_t begin = m_at;

                if (m_at < m_text.size() && m_text[m_at] == '-') {
                    ++m_at;
                }
                const std::size_t digits = m_at;

                while (m_at < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_at]))) {
                    ++m_at;
                }
                if (m_at == digits) {
                    m_at = begin;
                    fail("expected a value");
                }
                if (m_text[digits] == '0' && m_at - digits > 1) {
                    m_at = digits;
                    fail("a number does not start with 0");
                }
                if (m_at < m_text.size() && m_text[m_at] == '.') {
                    ++m_at;
                    const std::size_t fraction = m_at;

                    while (m_at < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_at]))) {
                        ++m_at;
                    }
                    if (m_at == fraction) {
                        fail("digits are expected after the point");
                    }
                }
                if (m_at < m_text.size() && (m_text[m_at] == 'e' || m_text[m_at] == 'E')) {
                    ++m_at;
                    if (m_at < m_text.size() && (m_text[m_at] == '+' || m_text[m_at] == '-')) {
                        ++m_at;
                    }
                    const std::size_t exponent = m_at;

                    while (m_at < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_at]))) {
                        ++m_at;
                    }
                    if (m_at == exponent) {
                        fail("digits are expected in the exponent");
                    }
                }
                double value = 0.0;
                const auto [end, error] = std::from_chars(m_text.data() + begin, m_text.data() + m_at, value);

                if (error != std::errc() || end != m_text.data() + m_at || !std::isfinite(value)) {
                    m_at = begin;
                    fail("this number cannot be read");
                }
                return Json(value);
            }

            static void appendUtf8(std::string& out, unsigned code)
            {
                if (code < 0x80) {
                    out += static_cast<char>(code);
                } else if (code < 0x800) {
                    out += static_cast<char>(0xC0 | (code >> 6));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                } else if (code < 0x10000) {
                    out += static_cast<char>(0xE0 | (code >> 12));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    out += static_cast<char>(0xF0 | (code >> 18));
                    out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                }
            }

            unsigned hex4(void)
            {
                if (m_at + 4 > m_text.size()) {
                    fail("a \\u needs four digits");
                }
                unsigned value = 0;

                for (int i = 0; i < 4; ++i) {
                    const char c = m_text[m_at++];

                    value <<= 4;
                    if (c >= '0' && c <= '9') {
                        value |= static_cast<unsigned>(c - '0');
                    } else if (c >= 'a' && c <= 'f') {
                        value |= static_cast<unsigned>(c - 'a' + 10);
                    } else if (c >= 'A' && c <= 'F') {
                        value |= static_cast<unsigned>(c - 'A' + 10);
                    } else {
                        --m_at;
                        fail("a \\u needs four hexadecimal digits");
                    }
                }
                return value;
            }

            std::string parseString(void)
            {
                expect('"');
                std::string out;

                while (true) {
                    if (m_at >= m_text.size()) {
                        fail("the string is not closed");
                    }
                    const char c = m_text[m_at++];

                    if (c == '"') {
                        return out;
                    }
                    if (static_cast<unsigned char>(c) < 0x20) {
                        --m_at;
                        fail("a control character in a string must be written \\uXXXX");
                    }
                    if (c != '\\') {
                        out += c;
                        continue;
                    }
                    if (m_at >= m_text.size()) {
                        fail("the string is not closed");
                    }
                    const char escaped = m_text[m_at++];

                    switch (escaped) {
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/': out += '/'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'n': out += '\n'; break;
                        case 'r': out += '\r'; break;
                        case 't': out += '\t'; break;
                        case 'u': {
                            unsigned code = hex4();

                            if (code >= 0xD800 && code <= 0xDBFF) {   // the first half of a pair
                                if (m_at + 1 < m_text.size() && m_text[m_at] == '\\' && m_text[m_at + 1] == 'u') {
                                    m_at += 2;
                                    const unsigned low = hex4();

                                    if (low < 0xDC00 || low > 0xDFFF) {
                                        fail("a high surrogate must be followed by a low one");
                                    }
                                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                                } else {
                                    fail("a high surrogate must be followed by a low one");
                                }
                            } else if (code >= 0xDC00 && code <= 0xDFFF) {
                                fail("a low surrogate alone");
                            }
                            appendUtf8(out, code);
                            break;
                        }
                        default:
                            --m_at;
                            fail(std::format("'\\{}' is not an escape", escaped));
                    }
                }
            }

            Json parseArray(int depth)
            {
                expect('[');
                Json::Array items;

                if (take(']')) {
                    return Json(std::move(items));
                }
                while (true) {
                    items.push_back(parseValue(depth + 1));
                    if (take(']')) {
                        return Json(std::move(items));
                    }
                    expect(',');
                }
            }

            Json parseObject(int depth)
            {
                expect('{');
                Json::Object members;

                if (take('}')) {
                    return Json(std::move(members));
                }
                while (true) {
                    skip();
                    if (m_at >= m_text.size() || m_text[m_at] != '"') {
                        fail("expected the name of a member");
                    }
                    std::string key = parseString();

                    expect(':');
                    if (members.count(key)) {
                        fail(std::format("the member '{}' is there twice", key));
                    }
                    members.emplace(std::move(key), parseValue(depth + 1));
                    if (take('}')) {
                        return Json(std::move(members));
                    }
                    expect(',');
                }
            }

            std::string_view m_text;
            std::size_t      m_at = 0;
    };

    void escape(std::string& out, const std::string& text)
    {
        out += '"';
        for (const char c : text) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                    } else {
                        out += c;
                    }
            }
        }
        out += '"';
    }

    void dumpValue(const Json& value, std::string& out, int indent, int level)
    {
        const auto newline = [&](int depth) {
            if (indent >= 0) {
                out += '\n';
                out.append(static_cast<std::size_t>(indent * depth), ' ');
            }
        };

        switch (value.type()) {
            case Json::Type::Null: out += "null"; break;
            case Json::Type::Bool: out += value.boolean() ? "true" : "false"; break;
            case Json::Type::Number: {
                const double number = value.number();
                char buffer[64];

                if (number == std::floor(number) && std::fabs(number) < 1e15) {
                    out += std::to_string(static_cast<long long>(number));
                } else {
                    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), number);

                    out.append(buffer, result.ptr);
                }
                break;
            }
            case Json::Type::String: escape(out, value.string()); break;
            case Json::Type::Array: {
                if (value.items().empty()) {
                    out += "[]";
                    break;
                }
                out += '[';
                bool first = true;

                for (const Json& item : value.items()) {
                    out += first ? "" : ",";
                    first = false;
                    newline(level + 1);
                    dumpValue(item, out, indent, level + 1);
                }
                newline(level);
                out += ']';
                break;
            }
            case Json::Type::Object: {
                if (value.members().empty()) {
                    out += "{}";
                    break;
                }
                out += '{';
                bool first = true;

                for (const auto& [key, item] : value.members()) {
                    out += first ? "" : ",";
                    first = false;
                    newline(level + 1);
                    escape(out, key);
                    out += indent >= 0 ? ": " : ":";
                    dumpValue(item, out, indent, level + 1);
                }
                newline(level);
                out += '}';
                break;
            }
        }
    }
}

const char* kuge::Json::name(Type type) noexcept
{
    switch (type) {
        case Type::Null: return "null";
        case Type::Bool: return "a boolean";
        case Type::Number: return "a number";
        case Type::String: return "a string";
        case Type::Array: return "an array";
        case Type::Object: return "an object";
    }
    return "?";
}

kuge::Json kuge::Json::parse(std::string_view text)
{
    return Parser(text).run();
}

kuge::Json kuge::Json::load(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);

    if (!in) {
        throw JsonError(std::format("{}: cannot be read", file.string()));
    }
    std::ostringstream text;

    text << in.rdbuf();
    try {
        return parse(text.str());
    } catch (const JsonError& error) {
        throw JsonError(std::format("{}: {}", file.string(), error.what()));
    }
}

std::string kuge::Json::dump(int indent) const
{
    std::string out;

    dumpValue(*this, out, indent, 0);
    return out;
}

bool kuge::Json::boolean(void) const
{
    if (m_type != Type::Bool) {
        throw JsonError(std::format("expected a boolean, found {}", name(m_type)));
    }
    return m_bool;
}

double kuge::Json::number(void) const
{
    if (m_type != Type::Number) {
        throw JsonError(std::format("expected a number, found {}", name(m_type)));
    }
    return m_number;
}

std::int64_t kuge::Json::integer(void) const
{
    const double value = number();

    if (value != std::floor(value) || std::fabs(value) > 9.0e15) {
        throw JsonError(std::format("expected a whole number, found {}", value));
    }
    return static_cast<std::int64_t>(value);
}

const std::string& kuge::Json::string(void) const
{
    if (m_type != Type::String) {
        throw JsonError(std::format("expected a string, found {}", name(m_type)));
    }
    return m_string;
}

const kuge::Json::Array& kuge::Json::items(void) const
{
    if (m_type != Type::Array) {
        throw JsonError(std::format("expected an array, found {}", name(m_type)));
    }
    return m_array;
}

const kuge::Json::Object& kuge::Json::members(void) const
{
    if (m_type != Type::Object) {
        throw JsonError(std::format("expected an object, found {}", name(m_type)));
    }
    return m_object;
}

const kuge::Json* kuge::Json::find(std::string_view key) const
{
    if (m_type != Type::Object) {
        return nullptr;
    }
    const auto found = m_object.find(key);

    return found == m_object.end() ? nullptr : &found->second;
}

const kuge::Json& kuge::Json::operator[](std::string_view key) const
{
    const Json* found = find(key);

    if (!found) {
        throw JsonError(std::format("there is no member '{}'", key));
    }
    return *found;
}

const kuge::Json& kuge::Json::at(std::size_t index) const
{
    if (index >= items().size()) {
        throw JsonError(std::format("there is no element {}", index));
    }
    return m_array[index];
}

std::size_t kuge::Json::size(void) const noexcept
{
    return m_type == Type::Array ? m_array.size() : (m_type == Type::Object ? m_object.size() : 0);
}

kuge::Json& kuge::Json::set(std::string key, Json value)
{
    if (m_type == Type::Null) {
        m_type = Type::Object;
    }
    if (m_type != Type::Object) {
        throw JsonError(std::format("cannot set a member of {}", name(m_type)));
    }
    m_object[std::move(key)] = std::move(value);
    return *this;
}

kuge::Json& kuge::Json::push(Json value)
{
    if (m_type == Type::Null) {
        m_type = Type::Array;
    }
    if (m_type != Type::Array) {
        throw JsonError(std::format("cannot add an element to {}", name(m_type)));
    }
    m_array.push_back(std::move(value));
    return *this;
}

bool kuge::Json::operator==(const Json& other) const
{
    if (m_type != other.m_type) {
        return false;
    }
    switch (m_type) {
        case Type::Null: return true;
        case Type::Bool: return m_bool == other.m_bool;
        case Type::Number: return m_number == other.m_number;
        case Type::String: return m_string == other.m_string;
        case Type::Array: return m_array == other.m_array;
        case Type::Object: return m_object == other.m_object;
    }
    return false;
}
