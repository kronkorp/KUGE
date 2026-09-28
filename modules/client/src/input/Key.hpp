#pragma once

#include <cstdint>
#include <string_view>

namespace kuge
{

    // Keys are named after their place on the keyboard, not the letter printed
    // on them: W is the key WASD games use, whatever the layout (AZERTY...).
    #define KUGE_KEYS(ITEM) \
        ITEM(A) ITEM(B) ITEM(C) ITEM(D) ITEM(E) ITEM(F) ITEM(G) ITEM(H) ITEM(I) ITEM(J) ITEM(K) ITEM(L) ITEM(M) \
        ITEM(N) ITEM(O) ITEM(P) ITEM(Q) ITEM(R) ITEM(S) ITEM(T) ITEM(U) ITEM(V) ITEM(W) ITEM(X) ITEM(Y) ITEM(Z) \
        ITEM(Num0) ITEM(Num1) ITEM(Num2) ITEM(Num3) ITEM(Num4) ITEM(Num5) ITEM(Num6) ITEM(Num7) ITEM(Num8) ITEM(Num9) \
        ITEM(F1) ITEM(F2) ITEM(F3) ITEM(F4) ITEM(F5) ITEM(F6) ITEM(F7) ITEM(F8) ITEM(F9) ITEM(F10) ITEM(F11) ITEM(F12) \
        ITEM(Up) ITEM(Down) ITEM(Left) ITEM(Right) \
        ITEM(Space) ITEM(Enter) ITEM(Escape) ITEM(Tab) ITEM(Backspace) ITEM(CapsLock) \
        ITEM(LShift) ITEM(RShift) ITEM(LCtrl) ITEM(RCtrl) ITEM(LAlt) ITEM(RAlt) \
        ITEM(Insert) ITEM(Delete) ITEM(Home) ITEM(End) ITEM(PageUp) ITEM(PageDown) \
        ITEM(Minus) ITEM(Equals) ITEM(LeftBracket) ITEM(RightBracket) ITEM(Semicolon) ITEM(Apostrophe) \
        ITEM(Comma) ITEM(Period) ITEM(Slash) ITEM(Backslash) ITEM(Grave) \
        ITEM(Kp0) ITEM(Kp1) ITEM(Kp2) ITEM(Kp3) ITEM(Kp4) ITEM(Kp5) ITEM(Kp6) ITEM(Kp7) ITEM(Kp8) ITEM(Kp9) \
        ITEM(KpEnter) ITEM(KpPlus) ITEM(KpMinus) ITEM(KpMultiply) ITEM(KpDivide) ITEM(KpPeriod)

    enum class Key : std::uint16_t {
        Unknown = 0,
        #define KUGE_KEY_ENUM(name) name,
        KUGE_KEYS(KUGE_KEY_ENUM)
        #undef KUGE_KEY_ENUM
        Count
    };

    #define KUGE_MOUSE_BUTTONS(ITEM) ITEM(Left) ITEM(Middle) ITEM(Right) ITEM(X1) ITEM(X2)

    enum class MouseButton : std::uint8_t {
        #define KUGE_MOUSE_ENUM(name) name,
        KUGE_MOUSE_BUTTONS(KUGE_MOUSE_ENUM)
        #undef KUGE_MOUSE_ENUM
        Count
    };

    #define KUGE_GAMEPAD_BUTTONS(ITEM) \
        ITEM(A) ITEM(B) ITEM(X) ITEM(Y) ITEM(Back) ITEM(Guide) ITEM(Start) ITEM(LeftStick) ITEM(RightStick) \
        ITEM(LeftShoulder) ITEM(RightShoulder) ITEM(DPadUp) ITEM(DPadDown) ITEM(DPadLeft) ITEM(DPadRight)

    enum class GamepadButton : std::uint8_t {
        #define KUGE_PAD_ENUM(name) name,
        KUGE_GAMEPAD_BUTTONS(KUGE_PAD_ENUM)
        #undef KUGE_PAD_ENUM
        Count
    };

    // Sticks go from -1 to 1, triggers from 0 to 1
    #define KUGE_GAMEPAD_AXES(ITEM) ITEM(LeftX) ITEM(LeftY) ITEM(RightX) ITEM(RightY) ITEM(LeftTrigger) ITEM(RightTrigger)

    enum class GamepadAxis : std::uint8_t {
        #define KUGE_AXIS_ENUM(name) name,
        KUGE_GAMEPAD_AXES(KUGE_AXIS_ENUM)
        #undef KUGE_AXIS_ENUM
        Count
    };

    //! "Space", "LShift", "F5"... "Unknown" for Key::Unknown
    const char* name(Key key) noexcept;
    const char* name(MouseButton button) noexcept;
    const char* name(GamepadButton button) noexcept;
    const char* name(GamepadAxis axis) noexcept;

    //! The way back, ignoring the case. Key::Unknown / Count if there is no such name.
    Key           keyFromName(std::string_view text) noexcept;
    MouseButton   mouseButtonFromName(std::string_view text) noexcept;
    GamepadButton gamepadButtonFromName(std::string_view text) noexcept;
    GamepadAxis   gamepadAxisFromName(std::string_view text) noexcept;

}
