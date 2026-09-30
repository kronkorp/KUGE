#pragma once

#include "Math2D.hpp"
#include "input/Key.hpp"
#include <string>
#include <variant>

namespace kuge
{

    // What a backend reports about the outside world, one entry per thing that
    // happened. The InputMap turns the ones about inputs into actions.

    struct KeyEvent {
        Key  key;
        bool down;
        bool repeat = false;   //!< The OS repeating a key that is held: ignored
    };

    struct MouseButtonEvent {
        MouseButton button;
        bool        down;
        Vec2        position;
    };

    struct MouseMoveEvent {
        Vec2 position;
        Vec2 delta;
    };

    struct MouseWheelEvent {
        Vec2 delta;
    };

    struct GamepadButtonEvent {
        int           pad;   //!< Which gamepad, when several are plugged
        GamepadButton button;
        bool          down;
    };

    struct GamepadAxisEvent {
        int         pad;
        GamepadAxis axis;
        float       value;   //!< -1 to 1 (0 to 1 for triggers)
    };

    struct GamepadConnectionEvent {
        int  pad;
        bool connected;
    };

    struct ResizeEvent {
        int width;
        int height;
    };

    //! The user asked to close the window
    struct QuitEvent {};

    //! Characters typed, as the keyboard layout and the OS make them (a key of the
    //! keyboard, a composed accent, an emoji...). UTF-8, one or more characters.
    //! It is text, not a key: no action reacts to it (see InputMap::takeTyped()).
    struct TextEvent {
        std::string text;
    };

    using Event = std::variant<
        KeyEvent, MouseButtonEvent, MouseMoveEvent, MouseWheelEvent,
        GamepadButtonEvent, GamepadAxisEvent, GamepadConnectionEvent,
        ResizeEvent, QuitEvent, TextEvent>;

}
