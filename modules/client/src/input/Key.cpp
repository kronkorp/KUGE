#include "input/Key.hpp"
#include <algorithm>
#include <array>
#include <cctype>

namespace
{
    bool sameName(std::string_view a, std::string_view b)
    {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
        });
    }

    // One table per enum, from the same lists as the enums
    constexpr std::array KEY_NAMES = {
        "Unknown",
        #define KUGE_NAME(name) #name,
        KUGE_KEYS(KUGE_NAME)
    };
    constexpr std::array MOUSE_NAMES = {
        KUGE_MOUSE_BUTTONS(KUGE_NAME)
    };
    constexpr std::array PAD_NAMES = {
        KUGE_GAMEPAD_BUTTONS(KUGE_NAME)
    };
    constexpr std::array AXIS_NAMES = {
        KUGE_GAMEPAD_AXES(KUGE_NAME)
    };
    #undef KUGE_NAME

    static_assert(KEY_NAMES.size() == static_cast<std::size_t>(kuge::Key::Count));
    static_assert(MOUSE_NAMES.size() == static_cast<std::size_t>(kuge::MouseButton::Count));
    static_assert(PAD_NAMES.size() == static_cast<std::size_t>(kuge::GamepadButton::Count));
    static_assert(AXIS_NAMES.size() == static_cast<std::size_t>(kuge::GamepadAxis::Count));

    template<typename Enum, typename Table>
    const char* nameIn(const Table& table, Enum value) noexcept
    {
        const auto index = static_cast<std::size_t>(value);

        return index < table.size() ? table[index] : "Unknown";
    }

    template<typename Enum, typename Table>
    Enum findIn(const Table& table, std::string_view text, std::size_t first, Enum none) noexcept
    {
        for (std::size_t i = first; i < table.size(); ++i) {
            if (sameName(table[i], text)) {
                return static_cast<Enum>(i);
            }
        }
        return none;
    }
}

const char* kuge::name(Key key) noexcept { return nameIn(KEY_NAMES, key); }
const char* kuge::name(MouseButton button) noexcept { return nameIn(MOUSE_NAMES, button); }
const char* kuge::name(GamepadButton button) noexcept { return nameIn(PAD_NAMES, button); }
const char* kuge::name(GamepadAxis axis) noexcept { return nameIn(AXIS_NAMES, axis); }

kuge::Key kuge::keyFromName(std::string_view text) noexcept
{
    // Starts at 1: "Unknown" is not a key one can bind
    return findIn(KEY_NAMES, text, 1, Key::Unknown);
}

kuge::MouseButton kuge::mouseButtonFromName(std::string_view text) noexcept
{
    return findIn(MOUSE_NAMES, text, 0, MouseButton::Count);
}

kuge::GamepadButton kuge::gamepadButtonFromName(std::string_view text) noexcept
{
    return findIn(PAD_NAMES, text, 0, GamepadButton::Count);
}

kuge::GamepadAxis kuge::gamepadAxisFromName(std::string_view text) noexcept
{
    return findIn(AXIS_NAMES, text, 0, GamepadAxis::Count);
}
