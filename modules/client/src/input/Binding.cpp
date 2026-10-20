#include "input/Binding.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>

namespace
{
    constexpr std::string_view MOUSE_PREFIX = "Mouse.";
    constexpr std::string_view PAD_PREFIX   = "Pad.";
    constexpr std::string_view WHEEL_PREFIX = "Wheel.";

    // "2", "-1", "0.5"
    std::optional<float> number(std::string_view text)
    {
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

        if (text.empty() || error != std::errc() || end != text.data() + text.size() || !std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    }

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
    const std::string sens = sensitivity == 1.0f ? std::string() : std::format("*{}", sensitivity);

    switch (kind) {
        case Kind::Key:
            return name(static_cast<Key>(code));
        case Kind::MouseButton:
            return std::string(MOUSE_PREFIX) + name(static_cast<MouseButton>(code));
        case Kind::GamepadButton:
            return std::string(PAD_PREFIX) + name(static_cast<GamepadButton>(code));
        case Kind::GamepadAxis:
            return std::string(PAD_PREFIX) + name(static_cast<GamepadAxis>(code)) + (direction < 0 ? '-' : '+') + sens;
        case Kind::MouseWheel:
            return std::string(WHEEL_PREFIX) + (direction < 0 ? "Down" : "Up");
        case Kind::MouseAxis:
            return std::string(MOUSE_PREFIX) + (code == 0 ? "X" : "Y") + sens;
    }
    return {};
}

std::optional<kuge::Binding> kuge::Binding::parse(std::string_view text)
{
    text = trim(text);
    // An axis can end with its sensitivity ("Mouse.X*2")
    float sensitivity = 1.0f;

    if (const auto star = text.find('*'); star != std::string_view::npos) {
        const auto value = number(trim(text.substr(star + 1)));

        if (!value) {
            return std::nullopt;
        }
        sensitivity = *value;
        text = trim(text.substr(0, star));
    }
    const bool scaled = sensitivity != 1.0f;

    if (startsWith(text, MOUSE_PREFIX)) {
        const std::string_view what = text.substr(MOUSE_PREFIX.size());

        if (what.size() == 1 && (what[0] == 'x' || what[0] == 'X' || what[0] == 'y' || what[0] == 'Y')) {
            return Binding::mouseAxis(what[0] == 'x' || what[0] == 'X' ? 0 : 1, sensitivity);
        }
        if (scaled) {
            return std::nullopt;   // (a button has no sensitivity)
        }
        const auto button = mouseButtonFromName(text.substr(MOUSE_PREFIX.size()));

        if (button == MouseButton::Count) {
            return std::nullopt;
        }
        return Binding(button);
    }
    if (scaled && !startsWith(text, PAD_PREFIX)) {
        return std::nullopt;
    }
    if (startsWith(text, WHEEL_PREFIX)) {
        const std::string_view way = text.substr(WHEEL_PREFIX.size());

        if (startsWith(way, "up") && way.size() == 2) {
            return Binding::wheel(1);
        }
        if (startsWith(way, "down") && way.size() == 4) {
            return Binding::wheel(-1);
        }
        return std::nullopt;
    }
    if (startsWith(text, PAD_PREFIX)) {
        std::string_view what = text.substr(PAD_PREFIX.size());

        if (!what.empty() && (what.back() == '+' || what.back() == '-')) {
            const int direction = what.back() == '-' ? -1 : 1;
            const auto axis = gamepadAxisFromName(what.substr(0, what.size() - 1));

            if (axis == GamepadAxis::Count) {
                return std::nullopt;
            }
            Binding binding = Binding::axis(axis, direction);

            binding.sensitivity = sensitivity;
            return binding;
        }
        if (scaled) {
            return std::nullopt;
        }
        const auto button = gamepadButtonFromName(what);

        if (button == GamepadButton::Count) {
            return std::nullopt;
        }
        return Binding(button);
    }
    const auto key = keyFromName(text);

    if (key == Key::Unknown || scaled) {
        return std::nullopt;
    }
    return Binding(key);
}
