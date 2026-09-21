#include "input/Binding.hpp"
#include <algorithm>
#include <cctype>

namespace
{
    constexpr std::string_view MOUSE_PREFIX = "Mouse.";
    constexpr std::string_view PAD_PREFIX   = "Pad.";

    std::string_view trim(std::string_view text)
    {
        const auto begin = text.find_first_not_of(" \t");

        if (begin == std::string_view::npos) {
            return {};
        }
        return text.substr(begin, text.find_last_not_of(" \t") - begin + 1);
    }

    // The names of the keys ignore the case, so the prefixes do too
    bool startsWith(std::string_view text, std::string_view prefix)
    {
        return text.size() >= prefix.size()
            && std::equal(prefix.begin(), prefix.end(), text.begin(), [](char expected, char found) {
                return std::tolower(static_cast<unsigned char>(expected)) == std::tolower(static_cast<unsigned char>(found));
            });
    }
}

std::string kuge::Binding::toString(void) const
{
    switch (kind) {
        case Kind::Key:
            return name(static_cast<Key>(code));
        case Kind::MouseButton:
            return std::string(MOUSE_PREFIX) + name(static_cast<MouseButton>(code));
        case Kind::GamepadButton:
            return std::string(PAD_PREFIX) + name(static_cast<GamepadButton>(code));
        case Kind::GamepadAxis:
            return std::string(PAD_PREFIX) + name(static_cast<GamepadAxis>(code)) + (direction < 0 ? '-' : '+');
    }
    return {};
}

std::optional<kuge::Binding> kuge::Binding::parse(std::string_view text)
{
    text = trim(text);
    if (startsWith(text, MOUSE_PREFIX)) {
        const auto button = mouseButtonFromName(text.substr(MOUSE_PREFIX.size()));

        if (button == MouseButton::Count) {
            return std::nullopt;
        }
        return Binding(button);
    }
    if (startsWith(text, PAD_PREFIX)) {
        std::string_view what = text.substr(PAD_PREFIX.size());

        if (!what.empty() && (what.back() == '+' || what.back() == '-')) {
            const int direction = what.back() == '-' ? -1 : 1;
            const auto axis = gamepadAxisFromName(what.substr(0, what.size() - 1));

            if (axis == GamepadAxis::Count) {
                return std::nullopt;
            }
            return Binding::axis(axis, direction);
        }
        const auto button = gamepadButtonFromName(what);

        if (button == GamepadButton::Count) {
            return std::nullopt;
        }
        return Binding(button);
    }
    const auto key = keyFromName(text);

    if (key == Key::Unknown) {
        return std::nullopt;
    }
    return Binding(key);
}
