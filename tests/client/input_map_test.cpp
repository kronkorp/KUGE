extern "C" {
    #include "kronklab/kronklab.h"
}
#include "input/InputMap.hpp"
#include "input/ViewAngles.hpp"
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

// Ctrl+V, C, X (Cmd on macOS) are editing keys for a text field; the same letters alone are text
Test(inputmap, clipboard_keys)
{
    kuge::InputMap input;

    input.handle(press(kuge::Key::V));
    input.handle(kuge::KeyEvent{kuge::Key::V, false});
    AssertEq(input.takeEditKeys().size(), 0, "a V alone is a letter");
    input.handle(press(kuge::Key::LCtrl));
    input.handle(press(kuge::Key::V));
    input.handle(press(kuge::Key::C));
    input.handle(kuge::KeyEvent{kuge::Key::LCtrl, false});
    input.handle(press(kuge::Key::X));
    input.handle(press(kuge::Key::RGui));
    input.handle(press(kuge::Key::X));
    const auto keys = input.takeEditKeys();

    AssertEq(keys.size(), 3, "V and C with Ctrl, X with Cmd, got %zu", keys.size());
    Assert(keys[0] == kuge::Key::V && keys[1] == kuge::Key::C && keys[2] == kuge::Key::X, "in order");
}

// -- The mouse wheel ---------------------------------------------------------------------------------------------
Test(inputmap, wheel_presses_an_action)
{
    kuge::InputMap input;
    const auto notch = [](float y) { return kuge::MouseWheelEvent{{0.0f, y}}; };

    input.bind(Action::Jump, kuge::Binding::wheel(1));
    input.bind(Action::Shoot, kuge::Binding::wheel(-1));
    input.handle(notch(1.0f));
    auto state = input.sampleTick(1);
    Assert(state.wasPressed(Action::Jump) && !state.wasPressed(Action::Shoot), "up presses the action bound to up");
    Assert(!state.isDown(Action::Jump), "for the tick only: it is not held");
    state = input.sampleTick(2);
    Assert(state.pressed == 0 && state.down == 0, "and not the next one");
    input.handle(notch(-2.0f));
    state = input.sampleTick(3);
    Assert(state.wasPressed(Action::Shoot) && !state.wasPressed(Action::Jump), "down presses the other");
    input.handle(notch(0.0f));
    Assert(input.sampleTick(4).pressed == 0, "a wheel that did not turn presses nothing");
}

Test(inputmap, wheel_notches_are_taken)
{
    kuge::InputMap input;

    input.handle(kuge::MouseWheelEvent{{0.0f, 1.0f}});
    input.handle(kuge::MouseWheelEvent{{0.0f, 2.0f}});
    input.handle(kuge::MouseWheelEvent{{0.0f, -0.5f}});
    AssertEq(input.takeWheel(), 2.5f, "the notches add up");
    AssertEq(input.takeWheel(), 0.0f, "and are given once");
    for (int i = 0; i < 1000; ++i) {
        input.handle(kuge::MouseWheelEvent{{0.0f, 1.0f}});
    }
    Assert(input.takeWheel() <= 100.0f, "what nobody reads does not pile up");
}

Test(inputmap, wheel_is_a_binding)
{
    AssertStrEq(kuge::Binding::wheel(1).toString().c_str(), "Wheel.Up", "up as text");
    AssertStrEq(kuge::Binding::wheel(-1).toString().c_str(), "Wheel.Down", "down as text");
    Assert(kuge::Binding::parse("wheel.up") == kuge::Binding::wheel(1), "read back, whatever the case");
    Assert(kuge::Binding::parse(" Wheel.Down ") == kuge::Binding::wheel(-1), "with spaces around");
    Assert(!kuge::Binding::parse("Wheel.Left").has_value(), "no other way");
    Assert(!kuge::Binding::parse("Wheel.Upper").has_value(), "not a prefix of a word");
    kuge::InputMap input;
    input.declare(Action::Jump, "up");
    input.bind(Action::Jump, kuge::Binding::wheel(1));
    kuge::ConfigFile config;
    input.save(config);
    kuge::InputMap other;
    other.declare(Action::Jump, "up");
    other.load(config);
    Assert(other.bindings(Action::Jump).size() == 1 && other.bindings(Action::Jump)[0] == kuge::Binding::wheel(1), "saved and loaded");
    input.startCapture();
    input.handle(kuge::MouseWheelEvent{{0.0f, -1.0f}});
    const auto captured = input.takeCaptured();
    Assert(captured.has_value() && *captured == kuge::Binding::wheel(-1), "and a rebinding can pick it");
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

Test(inputmap, mouse_axis_has_a_value)
{
    kuge::InputMap input;

    input.bind(Action::Left, kuge::Binding::mouseAxis(0));
    input.bind(Action::Right, kuge::Binding::mouseAxis(0, 2.0f));
    input.bind(Action::Jump, kuge::Binding::mouseAxis(1, -1.0f));
    input.handle(kuge::MouseMoveEvent{{10.0f, 4.0f}, {10.0f, -4.0f}});
    Assert(input.axisValue(Action::Left) == 10.0f, "x as it came");
    Assert(input.axisValue(Action::Right) == 20.0f, "twice with a sensitivity of 2");
    Assert(input.axisValue(Action::Jump) == 4.0f, "y inverted");
    Assert(input.axisValue(Action::Shoot) == 0.0f, "nothing for an action with no axis");
    Assert(input.sampleTick(1).down == 0, "a move is not a press");
    input.beginFrame();
    Assert(input.axisValue(Action::Left) == 0.0f, "and nothing the next frame");
}

Test(inputmap, stick_is_an_axis_too)
{
    kuge::InputMap input;

    input.bind(Action::Left, kuge::Binding::axis(kuge::GamepadAxis::RightX, 1));
    input.bind(Action::Left, kuge::Binding::axis(kuge::GamepadAxis::RightX, -1));
    input.handle(stick(0, kuge::GamepadAxis::RightX, 0.1f));
    Assert(input.axisValue(Action::Left) == 0.0f, "a stick at rest drifts a little: dead zone");
    input.handle(stick(0, kuge::GamepadAxis::RightX, 0.8f));
    Assert(input.axisValue(Action::Left) == 0.8f, "pushed right");
    input.handle(stick(0, kuge::GamepadAxis::RightX, -0.5f));
    Assert(input.axisValue(Action::Left) == -0.5f, "pushed left: the two bindings make one axis");
}

Test(inputmap, axes_in_the_keybinds_file)
{
    kuge::InputMap input;
    kuge::ConfigFile config;

    input.declare(Action::Left, "look_x");
    input.declare(Action::Right, "look_y");
    config.set("input.look_x", "Mouse.X*2.5, Pad.RightX+*3");
    config.set("input.look_y", "Mouse.Y*-1");
    input.load(config);
    Assert(input.bindings(Action::Left).size() == 2, "two inputs for the x look");
    Assert(input.bindings(Action::Left)[0] == kuge::Binding::mouseAxis(0, 2.5f), "the mouse, with its sensitivity");
    Assert(input.bindings(Action::Right)[0] == kuge::Binding::mouseAxis(1, -1.0f), "inverted y");

    kuge::ConfigFile saved;

    input.save(saved);
    Assert(saved.getString("input.look_x") == "Mouse.X*2.5, Pad.RightX+*3", "and they come back as they were: %s", saved.getString("input.look_x").c_str());
    Assert(!kuge::Binding::parse("Space*2"), "a key has no sensitivity");
    Assert(!kuge::Binding::parse("Mouse.X*fast"), "nor is a bad number one");
    Assert(kuge::Binding::parse("Mouse.X1") == kuge::Binding(kuge::MouseButton::X1), "(the button X1 is not the axis X)");
}

Test(inputmap, view_angles_clamp_and_pack)
{
    kuge::ViewAngles view;

    view.turn(0.0f, -100000.0f);
    Assert(view.pitch == kuge::ViewAngles::MAX_PITCH, "looking up stops at 89 degrees");
    view.turn(0.0f, 1000000.0f);
    Assert(view.pitch == -kuge::ViewAngles::MAX_PITCH, "and down too");
    view.turn(1000.0f, 0.0f, 1.0f);   // many turns
    Assert(view.yaw >= -3.1416f && view.yaw < 3.1416f, "yaw stays in one turn: %f", view.yaw);
    for (const float angle : {0.0f, 1.0f, -1.0f, 3.1f, -3.1f, 0.001f}) {
        const float back = kuge::unpackAngle(kuge::packAngle(angle));

        Assert(std::fabs(back - angle) < 0.0001f, "%f survives 16 bits, got %f", angle, back);
    }
    Assert(kuge::packAngle(7.0f) == kuge::packAngle(7.0f - 6.2831853f), "a turn more is the same angle");
}
