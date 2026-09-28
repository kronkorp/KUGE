#pragma once

#include <cstdint>

namespace kuge
{

    //! A color with its opacity, one byte each (0 to 255)
    struct Color
    {
        std::uint8_t r = 255;
        std::uint8_t g = 255;
        std::uint8_t b = 255;
        std::uint8_t a = 255;

        constexpr bool operator==(const Color&) const = default;
    };

    namespace colors
    {
        inline constexpr Color White       {255, 255, 255, 255};
        inline constexpr Color Black       {0, 0, 0, 255};
        inline constexpr Color Red         {230, 50, 50, 255};
        inline constexpr Color Green       {60, 200, 80, 255};
        inline constexpr Color Blue        {60, 110, 230, 255};
        inline constexpr Color Yellow      {240, 220, 60, 255};
        inline constexpr Color Transparent {0, 0, 0, 0};
    }

}
