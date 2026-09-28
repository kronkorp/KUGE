extern "C" {
    #include "kronklab/kronklab.h"
}
#include "input/Binding.hpp"
#include "input/Key.hpp"
#include <cstring>
#include <set>
#include <string>

// NOTE: kronklab test names are limited to 31 characters.

Test(keys, every_key_round_trips)
{
    std::set<std::string> names;

    for (std::size_t i = 1; i < static_cast<std::size_t>(kuge::Key::Count); ++i) {
        const auto key = static_cast<kuge::Key>(i);
        const std::string text = kuge::name(key);

        Assert(!text.empty(), "key %zu has a name", i);
        Assert(kuge::keyFromName(text) == key, "'%s' comes back as itself", text.c_str());
        Assert(names.insert(text).second, "'%s' is used by two keys", text.c_str());
    }
    AssertEq(names.size(), static_cast<std::size_t>(kuge::Key::Count) - 1, "one name per key");
}

// Regression: the lists that define the keys once had a parameter called X,
// which is also the name of the key X and of the gamepad button X
Test(keys, x_is_a_key_and_a_button)
{
    AssertStrEq(kuge::name(kuge::Key::X), "X", "the key");
    Assert(kuge::keyFromName("x") == kuge::Key::X, "found by its name");
    AssertStrEq(kuge::name(kuge::GamepadButton::X), "X", "the gamepad button");
    Assert(kuge::gamepadButtonFromName("x") == kuge::GamepadButton::X, "found by its name");
    Assert(kuge::Binding::parse("X") == kuge::Binding(kuge::Key::X), "as a binding");
    Assert(kuge::Binding::parse("Pad.X") == kuge::Binding(kuge::GamepadButton::X), "as a pad binding");
}

Test(keys, names_are_what_they_say)
{
    // Not only unique: each is the name of its enumerator
    AssertStrEq(kuge::name(kuge::Key::A), "A", "first letter");
    AssertStrEq(kuge::name(kuge::Key::Z), "Z", "last letter");
    AssertStrEq(kuge::name(kuge::Key::Num0), "Num0", "digit");
    AssertStrEq(kuge::name(kuge::Key::F12), "F12", "function key");
    AssertStrEq(kuge::name(kuge::Key::KpPeriod), "KpPeriod", "last key");
    AssertStrEq(kuge::name(kuge::MouseButton::X2), "X2", "last mouse button");
    AssertStrEq(kuge::name(kuge::GamepadButton::DPadRight), "DPadRight", "last gamepad button");
    AssertStrEq(kuge::name(kuge::GamepadAxis::RightTrigger), "RightTrigger", "last axis");
}

Test(keys, names_ignore_the_case)
{
    Assert(kuge::keyFromName("space") == kuge::Key::Space, "lower case");
    Assert(kuge::keyFromName("SPACE") == kuge::Key::Space, "upper case");
    Assert(kuge::keyFromName("lShIfT") == kuge::Key::LShift, "mixed case");
    Assert(kuge::keyFromName("w") == kuge::Key::W, "a letter");
}

Test(keys, unknown_names)
{
    Assert(kuge::keyFromName("") == kuge::Key::Unknown, "empty");
    Assert(kuge::keyFromName("NotAKey") == kuge::Key::Unknown, "made up");
    Assert(kuge::keyFromName("Unknown") == kuge::Key::Unknown, "Unknown is not a key to bind");
    Assert(kuge::keyFromName("Spac") == kuge::Key::Unknown, "a prefix is not enough");
    AssertStrEq(kuge::name(kuge::Key::Unknown), "Unknown", "Unknown has a name for logs");
    AssertStrEq(kuge::name(kuge::Key::Count), "Unknown", "out of range is safe");
}

Test(keys, buttons_and_axes_round_trip)
{
    for (std::size_t i = 0; i < static_cast<std::size_t>(kuge::MouseButton::Count); ++i) {
        const auto button = static_cast<kuge::MouseButton>(i);
        Assert(kuge::mouseButtonFromName(kuge::name(button)) == button, "mouse button %zu", i);
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(kuge::GamepadButton::Count); ++i) {
        const auto button = static_cast<kuge::GamepadButton>(i);
        Assert(kuge::gamepadButtonFromName(kuge::name(button)) == button, "gamepad button %zu", i);
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(kuge::GamepadAxis::Count); ++i) {
        const auto axis = static_cast<kuge::GamepadAxis>(i);
        Assert(kuge::gamepadAxisFromName(kuge::name(axis)) == axis, "gamepad axis %zu", i);
    }
    Assert(kuge::mouseButtonFromName("Nope") == kuge::MouseButton::Count, "unknown mouse button");
    Assert(kuge::gamepadButtonFromName("Nope") == kuge::GamepadButton::Count, "unknown gamepad button");
    Assert(kuge::gamepadAxisFromName("Nope") == kuge::GamepadAxis::Count, "unknown axis");
}

Test(binding, text_round_trip)
{
    const kuge::Binding all[] = {
        kuge::Binding(kuge::Key::Space),
        kuge::Binding(kuge::Key::LShift),
        kuge::Binding(kuge::MouseButton::Right),
        kuge::Binding(kuge::GamepadButton::DPadUp),
        kuge::Binding::axis(kuge::GamepadAxis::LeftX, -1),
        kuge::Binding::axis(kuge::GamepadAxis::RightTrigger, 1),
    };
    const char* texts[] = {"Space", "LShift", "Mouse.Right", "Pad.DPadUp", "Pad.LeftX-", "Pad.RightTrigger+"};

    for (std::size_t i = 0; i < 6; ++i) {
        AssertStrEq(all[i].toString().c_str(), texts[i], "as text");
        const auto back = kuge::Binding::parse(texts[i]);
        Assert(back.has_value() && *back == all[i], "'%s' comes back", texts[i]);
    }
}

Test(binding, parse_is_tolerant)
{
    Assert(kuge::Binding::parse("  space ") == kuge::Binding(kuge::Key::Space), "spaces");
    Assert(kuge::Binding::parse("mouse.left") == kuge::Binding(kuge::MouseButton::Left), "case");
    Assert(kuge::Binding::parse("PAD.a") == kuge::Binding(kuge::GamepadButton::A), "case of a pad");
}

Test(binding, parse_refuses_the_rest)
{
    Assert(!kuge::Binding::parse("").has_value(), "empty");
    Assert(!kuge::Binding::parse("   ").has_value(), "blank");
    Assert(!kuge::Binding::parse("NotAKey").has_value(), "unknown key");
    Assert(!kuge::Binding::parse("Mouse.Nope").has_value(), "unknown mouse button");
    Assert(!kuge::Binding::parse("Mouse.").has_value(), "no mouse button");
    Assert(!kuge::Binding::parse("Pad.").has_value(), "no pad input");
    Assert(!kuge::Binding::parse("Pad.LeftX*").has_value(), "bad direction");
    Assert(!kuge::Binding::parse("Pad.A+").has_value(), "a button has no direction");
    Assert(!kuge::Binding::parse("Pad.LeftX").has_value(), "an axis needs a direction");
}

Test(binding, axis_direction_is_a_sign)
{
    AssertEq(kuge::Binding::axis(kuge::GamepadAxis::LeftX, 5).direction, 1, "positive");
    AssertEq(kuge::Binding::axis(kuge::GamepadAxis::LeftX, -3).direction, -1, "negative");
}
