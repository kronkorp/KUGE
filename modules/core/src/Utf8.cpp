#include "Utf8.hpp"

bool kuge::utf8Valid(std::string_view text) noexcept
{
    std::size_t i = 0;

    while (i < text.size()) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        unsigned lowest = 0x80;   // a code point under this, in this many bytes, is an overlong form

        if (byte < 0x80) {
            ++i;
            continue;
        } else if (byte >= 0xC2 && byte <= 0xDF) {
            extra = 1;
        } else if (byte >= 0xE0 && byte <= 0xEF) {
            extra = 2;
            lowest = 0x800;
        } else if (byte >= 0xF0 && byte <= 0xF4) {
            extra = 3;
            lowest = 0x10000;
        } else {
            return false;
        }
        if (i + extra >= text.size()) {
            return false;   // cut
        }
        unsigned code = byte & (0x3Fu >> extra);

        for (std::size_t k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);

            if ((next & 0xC0) != 0x80) {
                return false;
            }
            code = (code << 6) | (next & 0x3Fu);
        }
        if (code < lowest || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

std::size_t kuge::utf8Length(std::string_view text) noexcept
{
    std::size_t count = 0;

    for (const char c : text) {
        count += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
    }
    return count;
}

std::string_view kuge::trimSpaces(std::string_view text) noexcept
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}
