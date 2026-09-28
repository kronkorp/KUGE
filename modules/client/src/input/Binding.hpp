#pragma once

#include "input/Key.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kuge
{

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  One physical input that can trigger an action: a key, a mouse
     *         button, a gamepad button, or a gamepad stick pushed one way
     *
     * As text (for the keybinds file): "Space", "Mouse.Left", "Pad.A",
     * "Pad.LeftX-" (stick to the left), "Pad.LeftTrigger+".
     */
    ////////////////////////////////////////////////////////////////////////////
    struct Binding
    {
        enum class Kind : std::uint8_t { Key, MouseButton, GamepadButton, GamepadAxis };

        Kind          kind      = Kind::Key;
        std::uint16_t code      = 0;   //!< The key / button / axis, as a number
        std::int8_t   direction = 0;   //!< Axes only: -1 or +1

        constexpr Binding(void) = default;

        // Not explicit: input.bind(Action::Jump, Key::Space)
        constexpr Binding(Key key) : kind(Kind::Key), code(static_cast<std::uint16_t>(key)) {}
        constexpr Binding(MouseButton button) : kind(Kind::MouseButton), code(static_cast<std::uint16_t>(button)) {}
        constexpr Binding(GamepadButton button) : kind(Kind::GamepadButton), code(static_cast<std::uint16_t>(button)) {}

        //! A stick or trigger pushed towards direction (-1 or +1)
        static constexpr Binding axis(GamepadAxis axis, int direction)
        {
            Binding binding;

            binding.kind = Kind::GamepadAxis;
            binding.code = static_cast<std::uint16_t>(axis);
            binding.direction = direction < 0 ? -1 : 1;
            return binding;
        }

        constexpr bool operator==(const Binding&) const = default;

        std::string toString(void) const;

        //! @return  nothing if the text is not a binding
        static std::optional<Binding> parse(std::string_view text);
    };

}
