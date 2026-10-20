#pragma once

#include <cstddef>
#include <string_view>

namespace kuge
{

    //! Is this well-formed UTF-8? Overlong forms, surrogates (U+D800 to U+DFFF), code points above U+10FFFF, a sequence that is
    //! cut and a byte that can never start one (0xC0, 0xC1, 0xF5 to 0xFF) are refused. Text from a client is not trusted.
    bool utf8Valid(std::string_view text) noexcept;

    //! How many characters (code points) there are. Only meaningful for text that is valid: a stray byte counts for one.
    std::size_t utf8Length(std::string_view text) noexcept;

    //! Without the spaces, tabs and line breaks at both ends (a view into the same text)
    std::string_view trimSpaces(std::string_view text) noexcept;

}
