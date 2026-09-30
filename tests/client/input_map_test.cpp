extern "C" {
    #include "kronklab/kronklab.h"
}
#include "input/InputMap.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    enum class Action : std::uint8_t { Jump, Shoot, Left, Right, Menu };

    kuge::Event press(kuge::Key key) { return kuge::KeyEvent{key, true}; }
    kuge::Event release(kuge::Key key) { return kuge::KeyEvent{key, false}; }
    kuge::Event repeat(kuge::Key key) { return kuge::KeyEvent{key, true, true}; }
    kuge::Event pad(int index, kuge::GamepadButton button, bool down) { return kuge::GamepadButtonEvent{index, button, down}; }
    kuge::Event stick(int index, kuge::GamepadAxis axis, float value) { return kuge::GamepadAxisEvent{index, axis, value}; }

    std::filesystem::path tempPath(const char* name)
    {
        return std::filesystem::temp_directory_path()
            / ("kuge_test_" + std::to_string(::getpid()) + "_" + name);
    }
}

Test(inputmap, press_hold_release)
{
    kuge::InputMap input;

    input.bind(Action::Jump, kuge::Key::Space);
    Assert(input.sampleTick(0).down == 0, "nothing at first");
    input.handle(press(kuge::Key::Space));
    auto state = input.sampleTick(1);
    Assert(state.isDown(Action::Jump) && state.wasPressed(Action::Jump), "pressed and held");
    Assert(!state.wasReleased(Action::Jump), "not released");
    state = input.sampleTick(2);
    Assert(state.isDown(Action::Jump) && !state.wasPressed(Action::Jump), "still held, no new press");
    input.handle(release(kuge::Key::Space));
    state = input.sampleTick(3);
    Assert(!state.isDown(Action::Jump) && state.wasReleased(Action::Jump), "released");
    state = input.sampleTick(4);
    Assert(state.down == 0 && state.pressed == 0 && state.released == 0, "and then nothing");
}

Test(inputmap, tap_between_two_ticks)
{
    kuge::InputMap input;

    input.bind(Action::Shoot, kuge::Key::Space);
    // Pressed and released before the simulation looked: still a shot
    input.handle(press(kuge::Key::Space));
    input.handle(release(kuge::Key::Space));
    auto state = input.sampleTick(1);
    Assert(state.wasPressed(Action::Shoot), "the tap is not lost");
    Assert(state.wasReleased(Action::Shoot), "and it ended");
    Assert(!state.isDown(Action::Shoot), "not held any more");
    Assert(input.sampleTick(2).pressed == 0, "reported once");
}

Test(inputmap, tick_number_is_kept)
{
    kuge::InputMap input;

    AssertEq(input.sampleTick(41).tick, 41, "the tick of the sample");
}

Test(inputmap, several_bindings_one_action)
{
    kuge::InputMap input;

    input.bind(Action::Shoot, kuge::Key::Space);
    input.bind(Action::Shoot, kuge::GamepadButton::A);
    input.handle(press(kuge::Key::Space));
    input.handle(pad(0, kuge::GamepadButton::A, true));
    input.handle(release(kuge::Key::Space));
    Assert(input.sampleTick(1).isDown(Action::Shoot), "still down: the pad holds it");
    input.handle(pad(0, kuge::GamepadButton::A, false));
    Assert(!input.sampleTick(2).isDown(Action::Shoot), "up when both are up");
    AssertEq(input.bindings(Action::Shoot).size(), 2, "two bindings");
    input.bind(Action::Shoot, kuge::Key::Space);
    AssertEq(input.bindings(Action::Shoot).size(), 2, "binding twice the same input does nothing");
}

Test(inputmap, one_input_two_actions)
{
    kuge::InputMap input;

    input.bind(Action::Jump, kuge::Key::W);
    input.bind(Action::Menu, kuge::Key::W);
    input.handle(press(kuge::Key::W));
    const auto state = input.sampleTick(1);
    Assert(state.isDown(Action::Jump) && state.isDown(Action::Menu), "both react");
}

Test(inputmap, os_key_repeat_is_ignored)
{
    kuge::InputMap input;

    input.bind(Action::Jump, kuge::Key::Space);
    input.handle(press(kuge::Key::Space));
    input.sampleTick(1);
    input.handle(repeat(kuge::Key::Space));
    input.handle(repeat(kuge::Key::Space));
    const auto state = input.sampleTick(2);
    Assert(state.isDown(Action::Jump), "still held");
    Assert(!state.wasPressed(Action::Jump), "a repeat is not a new press");
}

Test(inputmap, unknown_keys_do_nothing)
{
    kuge::InputMap input;

    input.bind(Action::Jump, kuge::Key::Space);
    input.handle(press(kuge::Key::Unknown));
    Assert(input.sampleTick(1).down == 0, "nothing bound to Unknown");
}

// What is typed is text: no action reacts to it, and a text field reads it apart
Test(inputmap, typed_text)
{
    kuge::InputMap input;

    input.bind(Action::Jump, kuge::Key::W);
    input.handle(kuge::TextEvent{"w"});
    input.handle(kuge::TextEvent{"\xC3\xA9"});
    Assert(input.sampleTick(1).down == 0, "the text of a W is not the key W: no action");
    AssertStrEq(input.takeTyped().c_str(), "w\xC3\xA9", "what was typed, in order");
    AssertStrEq(input.takeTyped().c_str(), "", "and only once");
    // Nobody reads it: it does not pile up
    for (int i = 0; i < 1000; ++i) {
        input.handle(kuge::TextEvent{"abcdefghij"});
    }
    Assert(input.takeTyped().size() <= 256, "what is kept is bounded");
    AssertStrEq(input.takeTyped().c_str(), "", "");
}

Test(inputmap, editing_keys)
{
    kuge::InputMap input;

    input.bind(Action::Jump, kuge::Key::Left);
    input.handle(press(kuge::Key::Backspace));
    input.handle(kuge::KeyEvent{kuge::Key::Backspace, true, true});    // the OS repeats the key held: it counts
    input.handle(press(kuge::Key::Left));
    input.handle(press(kuge::Key::A));                                 // a letter is not an editing key
    input.handle(kuge::KeyEvent{kuge::Key::Delete, false});            // a release is not either
    input.handle(press(kuge::Key::Enter));
    const auto keys = input.takeEditKeys();

    AssertEq(keys.size(), 4, "the editing keys pressed, got %zu", keys.size());
    Assert(keys[0] == kuge::Key::Backspace && keys[1] == kuge::Key::Backspace && keys[2] == kuge::Key::Left && keys[3] == kuge::Key::Enter, "with the repeat, in order");
    AssertEq(input.takeEditKeys().size(), 0, "and only once");
    Assert(input.sampleTick(1).wasPressed(Action::Jump), "the actions work as before: Left is bound");
    for (int i = 0; i < 1000; ++i) {
        input.handle(press(kuge::Key::Backspace));
    }
    Assert(input.takeEditKeys().size() <= 64, "what is kept is bounded");
}

Test(inputmap, sticks_need_a_push)
{
    kuge::InputMap input;

    input.bind(Action::Left, kuge::Binding::axis(kuge::GamepadAxis::LeftX, -1));
    input.bind(Action::Right, kuge::Binding::axis(kuge::GamepadAxis::LeftX, 1));
    input.handle(stick(0, kuge::GamepadAxis::LeftX, -0.4f));
    Assert(input.sampleTick(1).down == 0, "a light push is not enough");
    input.handle(stick(0, kuge::GamepadAxis::LeftX, -0.6f));
    auto state = input.sampleTick(2);
    Assert(state.isDown(Action::Left) && !state.isDown(Action::Right), "left");
    input.handle(stick(0, kuge::GamepadAxis::LeftX, 0.9f));
    state = input.sampleTick(3);
    Assert(!state.isDown(Action::Left) && state.isDown(Action::Right), "right");
    input.setAxisThreshold(0.95f);
    input.handle(stick(0, kuge::GamepadAxis::LeftX, 0.9f));
    Assert(input.sampleTick(4).down == 0, "the threshold can be raised");
}

Test(inputmap, several_gamepads)
{
    kuge::InputMap input;

    input.bind(Action::Jump, kuge::GamepadButton::A);
    input.handle(kuge::GamepadConnectionEvent{0, true});
    input.handle(kuge::GamepadConnectionEvent{1, true});
    input.handle(pad(0, kuge::GamepadButton::A, true));
    input.handle(pad(1, kuge::GamepadButton::A, true));
    input.handle(pad(0, kuge::GamepadButton::A, false));
    Assert(input.sampleTick(1).isDown(Action::Jump), "the second gamepad still holds it");
    input.handle(kuge::GamepadConnectionEvent{1, false});
    Assert(!input.sampleTick(2).isDown(Action::Jump), "unplugging it lets go");
}

Test(inputmap, mouse)
{
    kuge::InputMap input;

    input.bind(Action::Shoot, kuge::MouseButton::Left);
    input.handle(kuge::MouseMoveEvent{{10.0f, 20.0f}, {10.0f, 20.0f}});
    Assert(input.mousePosition() == kuge::Vec2(10.0f, 20.0f), "the position is tracked");
    input.handle(kuge::MouseButtonEvent{kuge::MouseButton::Left, true, {30.0f, 40.0f}});
    Assert(input.sampleTick(1).isDown(Action::Shoot), "a button acts");
    Assert(input.mousePosition() == kuge::Vec2(30.0f, 40.0f), "and gives the position");
    input.handle(kuge::MouseButtonEvent{kuge::MouseButton::Left, false, {30.0f, 40.0f}});
    Assert(!input.sampleTick(2).isDown(Action::Shoot), "released");
}

Test(inputmap, changing_bindings_while_held)
{
    kuge::InputMap input;

    input.handle(press(kuge::Key::Space));
    input.bind(Action::Jump, kuge::Key::Space);
    Assert(input.isDown(Action::Jump), "an action bound to a held key is held");
    Assert(input.sampleTick(1).pressed == 0, "but nobody pressed anything");
    input.unbind(Action::Jump);
    Assert(!input.isDown(Action::Jump), "no binding, not held");
    input.bind(Action::Jump, kuge::Key::Space);
    input.bind(Action::Jump, kuge::Key::Enter);
    input.unbind(Action::Jump, kuge::Key::Space);
    AssertEq(input.bindings(Action::Jump).size(), 1, "one binding removed");
    Assert(!input.isDown(Action::Jump), "and the action follows");
}

Test(inputmap, actions_are_below_64)
{
    kuge::InputMap input;
    bool threw = false;

    input.bind(63, kuge::Key::A);
    try { input.bind(64, kuge::Key::A); } catch (const std::out_of_range&) { threw = true; }
    Assert(threw, "64 is too much");
    threw = false;
    try { input.bind(-1, kuge::Key::A); } catch (const std::out_of_range&) { threw = true; }
    Assert(threw, "and so is a negative one");
    input.handle(press(kuge::Key::A));
    Assert(input.sampleTick(1).isDown(63), "the last action works");
}

Test(inputmap, action_names)
{
    kuge::InputMap input;
    int refused = 0;

    input.declare(Action::Jump, "jump");
    input.declare(Action::Jump, "hop");   // renaming is fine
    for (const char* bad : {"", "two words", "a=b", "a,b", "#a", "[a"}) {
        try { input.declare(Action::Shoot, bad); } catch (const std::invalid_argument&) { ++refused; }
    }
    AssertEq(refused, 6, "invalid names: refused, got %d", refused);
    try { input.declare(Action::Shoot, "hop"); } catch (const std::invalid_argument&) { ++refused; }
    AssertEq(refused, 7, "a name used by another action is refused");
}

Test(inputmap, capture_next_input)
{
    kuge::InputMap input;

    input.bind(Action::Jump, kuge::Key::G);
    input.startCapture();
    Assert(input.capturing(), "waiting");
    input.handle(release(kuge::Key::F));
    input.handle(repeat(kuge::Key::F));
    Assert(input.capturing(), "a release or a repeat is not a push");
    input.handle(press(kuge::Key::G));
    Assert(!input.capturing(), "done");
    const auto captured = input.takeCaptured();
    Assert(captured.has_value() && *captured == kuge::Binding(kuge::Key::G), "the key that was pushed");
    Assert(!input.takeCaptured().has_value(), "given once");
    Assert(input.sampleTick(1).pressed == 0, "and the action bound to it did not react");
}

Test(inputmap, capture_other_inputs)
{
    kuge::InputMap input;

    input.startCapture();
    input.handle(stick(0, kuge::GamepadAxis::LeftY, 0.3f));
    Assert(input.capturing(), "a light push is not a choice");
    input.handle(stick(0, kuge::GamepadAxis::LeftY, -0.9f));
    Assert(input.takeCaptured() == kuge::Binding::axis(kuge::GamepadAxis::LeftY, -1), "a stick, with its direction");
    input.startCapture();
    input.handle(kuge::MouseButtonEvent{kuge::MouseButton::Middle, true, {}});
    Assert(input.takeCaptured() == kuge::Binding(kuge::MouseButton::Middle), "a mouse button");
    input.startCapture();
    input.handle(pad(0, kuge::GamepadButton::Start, true));
    Assert(input.takeCaptured() == kuge::Binding(kuge::GamepadButton::Start), "a gamepad button");
    input.startCapture();
    input.cancelCapture();
    input.handle(press(kuge::Key::A));
    Assert(!input.takeCaptured().has_value() && !input.capturing(), "cancelled");
}

Test(inputmap, save_to_config)
{
    kuge::InputMap input;
    kuge::ConfigFile config;

    input.declare(Action::Shoot, "shoot");
    input.declare(Action::Jump, "jump");
    input.declare(Action::Menu, "menu");
    input.bind(Action::Shoot, kuge::Key::Space);
    input.bind(Action::Shoot, kuge::GamepadButton::A);
    input.bind(Action::Shoot, kuge::Binding::axis(kuge::GamepadAxis::RightTrigger, 1));
    input.bind(Action::Jump, kuge::Key::W);
    input.save(config);
    AssertStrEq(config.getString("input.shoot").c_str(), "Space, Pad.A, Pad.RightTrigger+", "in order");
    AssertStrEq(config.getString("input.jump").c_str(), "W", "one binding");
    Assert(config.has("input.menu") && config.getString("input.menu").empty(), "no binding is written too");
    Assert(!config.has("input.left"), "an action with no name is not written");
}

Test(inputmap, load_from_config)
{
    kuge::InputMap first;
    kuge::InputMap second;
    kuge::ConfigFile config;

    for (auto* map : {&first, &second}) {
        map->declare(Action::Shoot, "shoot");
        map->declare(Action::Jump, "jump");
    }
    first.bind(Action::Shoot, kuge::Key::Space);
    first.bind(Action::Shoot, kuge::MouseButton::Left);
    first.bind(Action::Jump, kuge::Key::W);
    first.save(config);
    second.load(config);
    Assert(second.bindings(Action::Shoot) == first.bindings(Action::Shoot), "shoot comes back");
    Assert(second.bindings(Action::Jump) == first.bindings(Action::Jump), "jump comes back");
}

Test(inputmap, load_only_what_is_there)
{
    kuge::InputMap input;
    kuge::ConfigFile config;

    input.declare(Action::Shoot, "shoot");
    input.declare(Action::Jump, "jump");
    input.declare(Action::Menu, "menu");
    input.bind(Action::Shoot, kuge::Key::Space);
    input.bind(Action::Jump, kuge::Key::W);
    input.bind(Action::Menu, kuge::Key::Escape);
    config.parse("input.shoot = F, Mouse.Left\ninput.jump =\ninput.unknown = Q\n");
    input.load(config);
    AssertEq(input.bindings(Action::Shoot).size(), 2, "shoot is replaced");
    Assert(input.bindings(Action::Shoot)[0] == kuge::Binding(kuge::Key::F), "by what the file says");
    Assert(input.bindings(Action::Jump).empty(), "an empty value unbinds");
    Assert(input.bindings(Action::Menu) == std::vector<kuge::Binding>{kuge::Binding(kuge::Key::Escape)},
        "menu is not in the file: it keeps its default");
}

Test(inputmap, load_skips_bad_inputs)
{
    kuge::InputMap input;
    kuge::ConfigFile config;

    input.declare(Action::Shoot, "shoot");
    config.parse("input.shoot = Space, NotAKey, , Space, Pad.A\n");
    input.load(config);
    AssertEq(input.bindings(Action::Shoot).size(), 2, "the bad one, the blank one and the duplicate are skipped");
    Assert(input.bindings(Action::Shoot)[1] == kuge::Binding(kuge::GamepadButton::A), "the others are kept");
}

Test(inputmap, keybinds_file)
{
    const auto path = tempPath("keybinds.cfg");
    kuge::InputMap input;

    input.declare(Action::Shoot, "shoot");
    input.bind(Action::Shoot, kuge::Key::Space);
    Assert(!input.loadBindings(path), "no file: false");
    Assert(input.bindings(Action::Shoot).size() == 1, "and nothing changed");
    input.saveBindings(path);
    Assert(std::filesystem::exists(path), "written");

    kuge::InputMap other;
    other.declare(Action::Shoot, "shoot");
    other.bind(Action::Shoot, kuge::Key::Q);
    Assert(other.loadBindings(path), "loaded");
    Assert(other.bindings(Action::Shoot) == input.bindings(Action::Shoot), "same bindings");

    {
        std::ofstream broken(path);
        broken << "this is not a config file\n";
    }
    Assert(!other.loadBindings(path), "an invalid file is refused");
    Assert(other.bindings(Action::Shoot) == input.bindings(Action::Shoot), "and the bindings stay");
    std::filesystem::remove(path);
}

// The file of the README: a comment has its own line, an empty value unbinds
Test(inputmap, readme_keybinds_file)
{
    const auto path = tempPath("readme_keybinds.cfg");
    kuge::InputMap input;

    input.declare(Action::Shoot, "shoot");
    input.declare(Action::Jump, "up");
    input.declare(Action::Menu, "pause");
    input.bind(Action::Menu, kuge::Key::Escape);
    {
        std::ofstream file(path);
        file << "input.shoot = Space, Pad.A\n"
             << "input.up    = W, Pad.DPadUp, Pad.LeftY-\n"
             << "# empty: nothing bound to pause\n"
             << "input.pause =\n";
    }
    Assert(input.loadBindings(path), "loaded");
    Assert(input.bindings(Action::Shoot).size() == 2, "shoot has its two inputs");
    Assert(input.bindings(Action::Jump).size() == 3, "up has its three inputs");
    Assert(input.bindings(Action::Menu).empty(), "pause has none");
    std::filesystem::remove(path);
}
