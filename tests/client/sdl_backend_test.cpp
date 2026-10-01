extern "C" {
    #include "kronklab/kronklab.h"
}
#include "ClientModule.hpp"
#include "ClientScene.hpp"
#include "Engine.hpp"
#include "backend/SdlBackend.hpp"
#include "sdl/SdlKeys.hpp"
#include <SDL.h>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    // No screen needed: SDL's own dummy driver, and drawing on the CPU
    void useNoScreen(void)
    {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("SDL_RENDER_DRIVER", "software", 1);
        setenv("SDL_AUDIODRIVER", "dummy", 1);
    }

    kuge::WindowConfig windowOf(int width, int height)
    {
        kuge::WindowConfig config;

        config.width = width;
        config.height = height;
        config.vsync = false;
        return config;
    }

    kuge::Color pixel(const kuge::Image& image, int x, int y)
    {
        const std::size_t at = (static_cast<std::size_t>(y) * image.width + x) * 4;

        return {image.rgba[at], image.rgba[at + 1], image.rgba[at + 2], image.rgba[at + 3]};
    }

    bool close(kuge::Color a, kuge::Color b, int tolerance = 3)
    {
        return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance
            && std::abs(a.b - b.b) <= tolerance && std::abs(a.a - b.a) <= tolerance;
    }

    void push(const SDL_Event& event)
    {
        SDL_Event copy = event;

        SDL_PushEvent(&copy);
    }

    template<typename T>
    const T* as(const kuge::Event& event) { return std::get_if<T>(&event); }

    // A 2x2 texture: red and green on top, blue and white below
    const std::uint8_t QUADS[] = {
        230, 50, 50, 255,     60, 200, 80, 255,
        60, 110, 230, 255,    255, 255, 255, 255,
    };
}

Test(sdl, every_key_is_reachable)
{
    std::map<kuge::Key, int> found;

    for (int scancode = 0; scancode < SDL_NUM_SCANCODES; ++scancode) {
        const kuge::Key key = kuge::keyFromScancode(scancode);

        if (key != kuge::Key::Unknown) {
            ++found[key];
        }
    }
    for (std::size_t i = 1; i < static_cast<std::size_t>(kuge::Key::Count); ++i) {
        const auto key = static_cast<kuge::Key>(i);

        Assert(found.count(key) == 1, "'%s' is given by %d scancodes (it must be 1)", kuge::name(key), found.count(key) ? found[key] : 0);
    }
}

Test(sdl, keys_are_where_they_should)
{
    Assert(kuge::keyFromScancode(SDL_SCANCODE_W) == kuge::Key::W, "W");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_A) == kuge::Key::A, "A");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_Z) == kuge::Key::Z, "Z");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_0) == kuge::Key::Num0, "0 comes after 9 on SDL, before 1 for us");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_1) == kuge::Key::Num1, "1");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_9) == kuge::Key::Num9, "9");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_KP_0) == kuge::Key::Kp0, "keypad 0");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_KP_9) == kuge::Key::Kp9, "keypad 9");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_F12) == kuge::Key::F12, "F12");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_RETURN) == kuge::Key::Enter, "Return is Enter");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_LSHIFT) == kuge::Key::LShift, "left shift");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_RSHIFT) == kuge::Key::RShift, "right shift");
    Assert(kuge::keyFromScancode(SDL_SCANCODE_PRINTSCREEN) == kuge::Key::Unknown, "what we do not know");
    Assert(kuge::keyFromScancode(-1) == kuge::Key::Unknown, "out of range");
}

Test(sdl, window_and_renderer)
{
    useNoScreen();
    auto backend = kuge::makeSdlBackend(windowOf(200, 100));

    Assert(backend.window->size() == kuge::Vec2(200.0f, 100.0f), "the size asked for");
    Assert(backend.renderer->outputSize() == kuge::Vec2(200.0f, 100.0f), "and the renderer draws on all of it");
    backend.window->setTitle("a title");
}

Test(sdl, fill_and_blend)
{
    useNoScreen();
    auto backend = kuge::makeSdlBackend(windowOf(200, 100));
    auto& renderer = *backend.renderer;
    const kuge::Color blue{60, 110, 230, 255};

    renderer.begin(blue);
    renderer.fillRect({10.0f, 10.0f, 20.0f, 20.0f}, kuge::colors::Red);
    renderer.fillRect({100.0f, 10.0f, 20.0f, 20.0f}, {255, 255, 255, 128});
    const auto image = renderer.readPixels();

    AssertEq(image.width, 200, "width");
    AssertEq(image.height, 100, "height");
    Assert(close(pixel(image, 15, 15), kuge::colors::Red), "filled");
    Assert(close(pixel(image, 5, 5), blue), "cleared");
    Assert(close(pixel(image, 29, 29), kuge::colors::Red), "up to the last pixel");
    Assert(close(pixel(image, 31, 31), blue), "and not beyond");
    Assert(close(pixel(image, 110, 20), {157, 182, 242, 255}), "half transparent white lets the blue through");
}

Test(sdl, outlines)
{
    useNoScreen();
    auto backend = kuge::makeSdlBackend(windowOf(200, 100));
    auto& renderer = *backend.renderer;
    const kuge::Color clear{0, 0, 0, 255};

    renderer.begin(clear);
    renderer.strokeRect({50.0f, 50.0f, 20.0f, 20.0f}, kuge::colors::White);
    const auto image = renderer.readPixels();

    Assert(close(pixel(image, 50, 50), kuge::colors::White), "the corner");
    Assert(close(pixel(image, 60, 50), kuge::colors::White), "the top edge");
    Assert(close(pixel(image, 60, 60), clear), "the inside is untouched");
}

Test(sdl, textures_stay_sharp)
{
    useNoScreen();
    auto backend = kuge::makeSdlBackend(windowOf(200, 100));
    auto& renderer = *backend.renderer;
    const kuge::TextureId id = renderer.createTexture(2, 2, QUADS);

    renderer.begin(kuge::colors::Black);
    kuge::TextureDraw draw;
    draw.texture = id;
    draw.destination = {10.0f, 10.0f, 20.0f, 20.0f};
    renderer.drawTexture(draw);
    const auto image = renderer.readPixels();

    Assert(close(pixel(image, 12, 12), {230, 50, 50, 255}), "top left of the picture");
    Assert(close(pixel(image, 27, 12), {60, 200, 80, 255}), "top right: no blur in between");
    Assert(close(pixel(image, 12, 27), {60, 110, 230, 255}), "bottom left");
    Assert(close(pixel(image, 27, 27), {255, 255, 255, 255}), "bottom right");
    Assert(close(pixel(image, 19, 12), {230, 50, 50, 255}) && close(pixel(image, 20, 12), {60, 200, 80, 255}),
        "the edge between two pixels is sharp");
}

Test(sdl, texture_options)
{
    useNoScreen();
    auto backend = kuge::makeSdlBackend(windowOf(200, 100));
    auto& renderer = *backend.renderer;
    const kuge::TextureId quads = renderer.createTexture(2, 2, QUADS);
    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    const kuge::TextureId white = renderer.createTexture(1, 1, whitePixel);
    kuge::TextureDraw draw;

    renderer.begin(kuge::colors::Black);

    draw.texture = white;
    draw.destination = {0.0f, 0.0f, 10.0f, 10.0f};
    draw.tint = {255, 0, 0, 255};
    renderer.drawTexture(draw);                                    // tinted red

    draw.destination = {20.0f, 0.0f, 10.0f, 10.0f};
    draw.tint = {255, 255, 255, 128};
    renderer.drawTexture(draw);                                    // half transparent

    draw = {};
    draw.texture = quads;
    draw.source = {1.0f, 0.0f, 1.0f, 1.0f};                        // only the green pixel
    draw.destination = {40.0f, 0.0f, 10.0f, 10.0f};
    renderer.drawTexture(draw);

    draw = {};
    draw.texture = quads;
    draw.destination = {60.0f, 0.0f, 10.0f, 10.0f};
    draw.flipX = true;                                             // green on the left now
    renderer.drawTexture(draw);

    draw = {};
    draw.texture = quads;
    draw.destination = {80.0f, 40.0f, 20.0f, 20.0f};
    draw.rotation = 90.0f;                                         // a quarter turn, clockwise
    renderer.drawTexture(draw);

    const auto image = renderer.readPixels();

    Assert(close(pixel(image, 5, 5), {255, 0, 0, 255}), "tinted");
    Assert(close(pixel(image, 25, 5), {128, 128, 128, 255}), "half transparent over black");
    Assert(close(pixel(image, 45, 5), {60, 200, 80, 255}) && close(pixel(image, 49, 9), {60, 200, 80, 255}), "a part of the picture, stretched");
    Assert(close(pixel(image, 62, 2), {60, 200, 80, 255}) && close(pixel(image, 67, 2), {230, 50, 50, 255}), "flipped");
    // A quarter turn clockwise moves each quarter to the next corner:
    // top left -> top right -> bottom right -> bottom left -> top left
    Assert(close(pixel(image, 95, 45), {230, 50, 50, 255}), "turned: red went from the top left to the top right");
    Assert(close(pixel(image, 95, 55), {60, 200, 80, 255}), "green went to the bottom right");
    Assert(close(pixel(image, 85, 55), {255, 255, 255, 255}), "white went to the bottom left");
    Assert(close(pixel(image, 85, 45), {60, 110, 230, 255}), "blue went to the top left");
}

Test(sdl, textures_can_go)
{
    useNoScreen();
    auto backend = kuge::makeSdlBackend(windowOf(100, 100));
    auto& renderer = *backend.renderer;
    const kuge::TextureId id = renderer.createTexture(2, 2, QUADS);
    kuge::TextureDraw draw;

    draw.texture = id;
    draw.destination = {0.0f, 0.0f, 20.0f, 20.0f};
    renderer.destroyTexture(id);
    renderer.destroyTexture(id);                       // twice: nothing happens
    renderer.destroyTexture(kuge::NO_TEXTURE);
    renderer.begin(kuge::colors::Black);
    renderer.drawTexture(draw);                        // drawing what is gone: nothing
    Assert(close(pixel(renderer.readPixels(), 5, 5), kuge::colors::Black), "nothing was drawn");
}

Test(sdl, bad_textures_are_refused)
{
    useNoScreen();
    auto backend = kuge::makeSdlBackend(windowOf(100, 100));
    const std::uint8_t few[3] = {1, 2, 3};
    bool refused = false;

    try { backend.renderer->createTexture(1, 1, few); } catch (const std::runtime_error&) { refused = true; }
    Assert(refused, "3 bytes are not a pixel");
    refused = false;
    try { backend.renderer->createTexture(0, 5, {}); } catch (const std::runtime_error&) { refused = true; }
    Assert(refused, "no size");
}

Test(sdl, keyboard_and_mouse_events)
{
    useNoScreen();
    auto backend = kuge::makeSdlBackend(windowOf(100, 100));
    std::vector<kuge::Event> events;
    SDL_Event sdl = {};

    backend.input->poll(events);                       // whatever came with the window
    events.clear();

    sdl.type = SDL_KEYDOWN;
    sdl.key.keysym.scancode = SDL_SCANCODE_W;
    sdl.key.repeat = 0;
    push(sdl);
    sdl.key.repeat = 1;
    push(sdl);
    sdl.type = SDL_KEYUP;
    sdl.key.repeat = 0;
    push(sdl);
    sdl = {};
    sdl.type = SDL_MOUSEMOTION;
    sdl.motion.x = 30; sdl.motion.y = 40; sdl.motion.xrel = 3; sdl.motion.yrel = -4;
    push(sdl);
    sdl = {};
    sdl.type = SDL_MOUSEBUTTONDOWN;
    sdl.button.button = SDL_BUTTON_RIGHT; sdl.button.x = 5; sdl.button.y = 6;
    push(sdl);
    sdl.type = SDL_MOUSEBUTTONUP;
    push(sdl);
    sdl = {};
    sdl.type = SDL_MOUSEWHEEL;
    sdl.wheel.preciseX = 0.0f; sdl.wheel.preciseY = -1.5f;
    push(sdl);
    backend.input->poll(events);

    AssertEq(events.size(), 7, "7 events, got %zu", events.size());
    const auto* down = as<kuge::KeyEvent>(events[0]);
    Assert(down && down->key == kuge::Key::W && down->down && !down->repeat, "W goes down");
    const auto* again = as<kuge::KeyEvent>(events[1]);
    Assert(again && again->repeat, "then the OS repeats it");
    const auto* up = as<kuge::KeyEvent>(events[2]);
    Assert(up && up->key == kuge::Key::W && !up->down, "W goes up");
    const auto* move = as<kuge::MouseMoveEvent>(events[3]);
    Assert(move && move->position == kuge::Vec2(30.0f, 40.0f) && move->delta == kuge::Vec2(3.0f, -4.0f), "the mouse moves");
    const auto* click = as<kuge::MouseButtonEvent>(events[4]);
    Assert(click && click->button == kuge::MouseButton::Right && click->down && click->position == kuge::Vec2(5.0f, 6.0f), "a click");
    const auto* release = as<kuge::MouseButtonEvent>(events[5]);
    Assert(release && !release->down, "and its release");
    const auto* wheel = as<kuge::MouseWheelEvent>(events[6]);
    Assert(wheel && wheel->delta == kuge::Vec2(0.0f, -1.5f), "the wheel");
    events.clear();
    backend.input->poll(events);
    AssertEq(events.size(), 0, "each event is given once");
}

// What is typed comes as text, in UTF-8, apart from the keys
Test(sdl, typed_text)
{
    useNoScreen();
    auto backend = kuge::makeSdlBackend(windowOf(100, 100));
    std::vector<kuge::Event> events;
    SDL_Event sdl = {};

    backend.input->poll(events);
    events.clear();
    sdl.type = SDL_TEXTINPUT;
    std::strcpy(sdl.text.text, "a");
    push(sdl);
    std::strcpy(sdl.text.text, "\xC3\xA9\xF0\x9F\x8E\xAE");   // an accent and an emoji, composed or pasted
    push(sdl);
    std::strcpy(sdl.text.text, "");                             // nothing: nothing to tell
    push(sdl);
    backend.input->poll(events);

    AssertEq(events.size(), 2, "two events of text, got %zu", events.size());
    const auto* first = as<kuge::TextEvent>(events[0]);
    const auto* second = as<kuge::TextEvent>(events[1]);

    Assert(first && first->text == "a", "a letter");
    Assert(second && second->text == "\xC3\xA9\xF0\x9F\x8E\xAE", "several characters, UTF-8, whole");
}

Test(sdl, window_and_gamepad_events)
{
    useNoScreen();
    auto backend = kuge::makeSdlBackend(windowOf(100, 100));
    std::vector<kuge::Event> events;
    SDL_Event sdl = {};

    backend.input->poll(events);
    events.clear();

    sdl.type = SDL_QUIT;
    push(sdl);
    sdl = {};
    sdl.type = SDL_WINDOWEVENT;
    sdl.window.event = SDL_WINDOWEVENT_CLOSE;
    push(sdl);
    sdl.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    sdl.window.data1 = 640; sdl.window.data2 = 360;
    push(sdl);
    sdl.window.event = SDL_WINDOWEVENT_MOVED;         // not something we care about
    push(sdl);
    sdl = {};
    sdl.type = SDL_CONTROLLERBUTTONDOWN;
    sdl.cbutton.which = 7; sdl.cbutton.button = SDL_CONTROLLER_BUTTON_A;
    push(sdl);
    sdl.type = SDL_CONTROLLERBUTTONUP;
    sdl.cbutton.button = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    push(sdl);
    sdl = {};
    sdl.type = SDL_CONTROLLERAXISMOTION;
    sdl.caxis.which = 7; sdl.caxis.axis = SDL_CONTROLLER_AXIS_LEFTX; sdl.caxis.value = 16384;
    push(sdl);
    sdl.caxis.axis = SDL_CONTROLLER_AXIS_TRIGGERLEFT; sdl.caxis.value = 32767;
    push(sdl);
    sdl.caxis.axis = SDL_CONTROLLER_AXIS_LEFTY; sdl.caxis.value = -32768;
    push(sdl);
    backend.input->poll(events);

    AssertEq(events.size(), 8, "8 events, got %zu", events.size());
    Assert(as<kuge::QuitEvent>(events[0]) != nullptr, "quit");
    Assert(as<kuge::QuitEvent>(events[1]) != nullptr, "closing the window is a quit too");
    const auto* resize = as<kuge::ResizeEvent>(events[2]);
    Assert(resize && resize->width == 640 && resize->height == 360, "a new size");
    const auto* pressed = as<kuge::GamepadButtonEvent>(events[3]);
    Assert(pressed && pressed->pad == 7 && pressed->button == kuge::GamepadButton::A && pressed->down, "A on pad 7");
    const auto* released = as<kuge::GamepadButtonEvent>(events[4]);
    Assert(released && released->button == kuge::GamepadButton::DPadLeft && !released->down, "d-pad left released");
    const auto* stick = as<kuge::GamepadAxisEvent>(events[5]);
    Assert(stick && stick->axis == kuge::GamepadAxis::LeftX && stick->value > 0.49f && stick->value < 0.51f, "a stick, in -1..1");
    const auto* trigger = as<kuge::GamepadAxisEvent>(events[6]);
    Assert(trigger && trigger->axis == kuge::GamepadAxis::LeftTrigger && trigger->value == 1.0f, "a trigger, in 0..1");
    const auto* low = as<kuge::GamepadAxisEvent>(events[7]);
    Assert(low && low->value == -1.0f, "the lowest value does not go beyond -1");
}

namespace
{
    struct Red : kuge::ClientScene
    {
        void onEnter(void) override
        {
            installClientSystems();
            const kw::Entity entity = world().create();
            kuge::Sprite sprite;

            sprite.size = {100.0f, 60.0f};
            sprite.tint = kuge::colors::Red;
            world().add<kuge::Transform2D>(entity, kuge::Transform2D{});
            world().add<kuge::Sprite>(entity, sprite);
        }
    };
}

Test(sdl, a_whole_engine)
{
    useNoScreen();
    kuge::Engine engine(kuge::Engine::Config{.mode = kuge::Engine::Mode::Windowed});
    auto& client = engine.addModule<kuge::ClientModule>(kuge::makeSdlBackend(windowOf(320, 200)),
        kuge::ClientConfig{kuge::Color{0, 0, 0, 255}});

    engine.scenes().change<Red>();
    client.requestScreenshot();
    AssertEq(engine.step(1.0 / 60.0), true, "the engine runs");
    const auto shot = client.takeScreenshot();

    Assert(shot.has_value() && shot->width == 320 && shot->height == 200, "a screenshot of the window");
    Assert(close(pixel(*shot, 160, 100), kuge::colors::Red), "the sprite, at the center");
    Assert(close(pixel(*shot, 111, 71), kuge::colors::Red), "up to its corner (110..210 by 70..130)");
    Assert(close(pixel(*shot, 100, 100), {0, 0, 0, 255}), "and the background around it");

    SDL_Event quit = {};
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit);
    AssertEq(engine.step(1.0 / 60.0), false, "closing the window ends the engine");
}
