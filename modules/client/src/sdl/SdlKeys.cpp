#include "sdl/SdlKeys.hpp"
#include <SDL.h>

namespace
{
    // Keys that follow each other in Key, as their scancodes do
    kuge::Key offset(kuge::Key first, int by) noexcept
    {
        return static_cast<kuge::Key>(static_cast<int>(first) + by);
    }
}

kuge::Key kuge::keyFromScancode(int scancode) noexcept
{
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        return offset(Key::A, scancode - SDL_SCANCODE_A);
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
        return offset(Key::Num1, scancode - SDL_SCANCODE_1);
    }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12) {
        return offset(Key::F1, scancode - SDL_SCANCODE_F1);
    }
    if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9) {
        return offset(Key::Kp1, scancode - SDL_SCANCODE_KP_1);
    }
    switch (scancode) {
        case SDL_SCANCODE_0:            return Key::Num0;
        case SDL_SCANCODE_KP_0:         return Key::Kp0;
        case SDL_SCANCODE_UP:           return Key::Up;
        case SDL_SCANCODE_DOWN:         return Key::Down;
        case SDL_SCANCODE_LEFT:         return Key::Left;
        case SDL_SCANCODE_RIGHT:        return Key::Right;
        case SDL_SCANCODE_SPACE:        return Key::Space;
        case SDL_SCANCODE_RETURN:       return Key::Enter;
        case SDL_SCANCODE_ESCAPE:       return Key::Escape;
        case SDL_SCANCODE_TAB:          return Key::Tab;
        case SDL_SCANCODE_BACKSPACE:    return Key::Backspace;
        case SDL_SCANCODE_CAPSLOCK:     return Key::CapsLock;
        case SDL_SCANCODE_LSHIFT:       return Key::LShift;
        case SDL_SCANCODE_RSHIFT:       return Key::RShift;
        case SDL_SCANCODE_LCTRL:        return Key::LCtrl;
        case SDL_SCANCODE_RCTRL:        return Key::RCtrl;
        case SDL_SCANCODE_LALT:         return Key::LAlt;
        case SDL_SCANCODE_RALT:         return Key::RAlt;
        case SDL_SCANCODE_INSERT:       return Key::Insert;
        case SDL_SCANCODE_DELETE:       return Key::Delete;
        case SDL_SCANCODE_HOME:         return Key::Home;
        case SDL_SCANCODE_END:          return Key::End;
        case SDL_SCANCODE_PAGEUP:       return Key::PageUp;
        case SDL_SCANCODE_PAGEDOWN:     return Key::PageDown;
        case SDL_SCANCODE_MINUS:        return Key::Minus;
        case SDL_SCANCODE_EQUALS:       return Key::Equals;
        case SDL_SCANCODE_LEFTBRACKET:  return Key::LeftBracket;
        case SDL_SCANCODE_RIGHTBRACKET: return Key::RightBracket;
        case SDL_SCANCODE_SEMICOLON:    return Key::Semicolon;
        case SDL_SCANCODE_APOSTROPHE:   return Key::Apostrophe;
        case SDL_SCANCODE_COMMA:        return Key::Comma;
        case SDL_SCANCODE_PERIOD:       return Key::Period;
        case SDL_SCANCODE_SLASH:        return Key::Slash;
        case SDL_SCANCODE_BACKSLASH:    return Key::Backslash;
        case SDL_SCANCODE_GRAVE:        return Key::Grave;
        case SDL_SCANCODE_KP_ENTER:     return Key::KpEnter;
        case SDL_SCANCODE_KP_PLUS:      return Key::KpPlus;
        case SDL_SCANCODE_KP_MINUS:     return Key::KpMinus;
        case SDL_SCANCODE_KP_MULTIPLY:  return Key::KpMultiply;
        case SDL_SCANCODE_KP_DIVIDE:    return Key::KpDivide;
        case SDL_SCANCODE_KP_PERIOD:    return Key::KpPeriod;
        default:                        return Key::Unknown;
    }
}
