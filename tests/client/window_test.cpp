extern "C" {
    #include "kronklab/kronklab.h"
}
#include "backend/SdlBackend.hpp"
#include "client_fixture.hpp"
#include <cmath>
#include <cstdlib>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    template<typename T>
    const T* find(const std::vector<kuge::Event>& events)
    {
        for (const kuge::Event& event : events) {
            if (const auto* found = std::get_if<T>(&event)) {
                return found;
            }
        }
        return nullptr;
    }
}

Test(window, sdl_follows_set_size)
{
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_RENDER_DRIVER", "software", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    kuge::WindowConfig config;

    config.width = 320;
    config.height = 200;
    config.vsync = false;
    auto backend = kuge::makeSdlBackend(config);
    std::vector<kuge::Event> events;

    backend.window->setSize(800, 600);
    backend.input->poll(events);   // (SDL tells its window events when they are pumped)
    Assert(backend.window->size().x == 800.0f && backend.window->size().y == 600.0f, "the window is 800 x 600");
    const auto* resize = find<kuge::ResizeEvent>(events);
    Assert(resize && resize->width == 800 && resize->height == 600, "and a ResizeEvent said so");
    Assert(backend.renderer->outputSize().x == 800.0f, "the renderer draws on the new size");
}

Test(window, sdl_fullscreen_round_trip)
{
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_RENDER_DRIVER", "software", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    kuge::WindowConfig config;

    config.width = 320;
    config.height = 200;
    config.vsync = false;
    auto backend = kuge::makeSdlBackend(config);
    std::vector<kuge::Event> events;

    Assert(!backend.window->fullscreen(), "it starts in a window");
    backend.window->setFullscreen(true);
    Assert(backend.window->fullscreen(), "fullscreen");
    backend.input->poll(events);
    backend.window->setFullscreen(false);
    backend.input->poll(events);
    Assert(!backend.window->fullscreen(), "and back in a window");
    Assert(backend.window->size().x == 320.0f && backend.window->size().y == 200.0f, "of the size it had");
    backend.window->setVsync(true);   // (it must not throw)
}

Test(window, dummy_follows_set_size)
{
    Fixture fx;

    fx.client->window().setSize(1024, 768);
    Assert(fx.client->window().size().x == 1024.0f, "the window is 1024 wide");
    Assert(fx.dummy.renderer->outputSize().y == 768.0f, "the renderer follows");
    fx.client->window().setFullscreen(true);
    Assert(fx.client->window().fullscreen(), "fullscreen is remembered");
    fx.client->window().setFullscreen(false);
    Assert(!fx.client->window().fullscreen(), "and given back");
}

Test(window, dummy_base_resolution)
{
    auto dummy = kuge::makeDummyBackend({2560.0f, 1440.0f});

    dummy.window->setBaseResolution(1280);
    Assert(dummy.window->size() == kuge::Vec2(2560.0f, 1440.0f) && dummy.renderer->outputSize() == kuge::Vec2(2560.0f, 1440.0f),
        "a window has no base resolution");
    dummy.window->setFullscreen(true);
    Assert(dummy.window->size() == kuge::Vec2(1280.0f, 720.0f), "in fullscreen the window says the base size, the height from the ratio");
    Assert(dummy.renderer->outputSize() == kuge::Vec2(1280.0f, 720.0f), "and so does the renderer");
    dummy.window->setFullscreen(false);
    Assert(dummy.window->size() == kuge::Vec2(2560.0f, 1440.0f) && dummy.renderer->outputSize() == kuge::Vec2(2560.0f, 1440.0f),
        "back in a window: the real size");
    dummy.window->setFullscreen(true);
    dummy.window->setBaseResolution(0);
    Assert(dummy.window->size() == kuge::Vec2(2560.0f, 1440.0f), "none: the pixels of the screen");
    dummy.window->setBaseResolution(640);
    dummy.window->setSize(1920, 1200);
    Assert(dummy.window->size() == kuge::Vec2(640.0f, 400.0f), "a screen of another ratio: the height follows it");
}

Test(window, sdl_base_resolution)
{
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_RENDER_DRIVER", "software", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    kuge::WindowConfig config;

    config.width = 320;
    config.height = 200;
    config.vsync = false;
    config.baseResolution = 400;
    auto backend = kuge::makeSdlBackend(config);
    std::vector<kuge::Event> events;

    Assert(backend.window->size() == kuge::Vec2(320.0f, 200.0f), "a window has no base resolution");
    backend.window->setFullscreen(true);
    backend.input->poll(events);
    const kuge::Vec2 base = backend.window->size();

    AssertEq(base.x, 400.0f, "in fullscreen: the base width");
    Assert(base.y > 0.0f && backend.renderer->outputSize() == base, "the renderer says it too");
    const auto* resize = find<kuge::ResizeEvent>(events);

    Assert(resize && resize->width == 400 && resize->height == static_cast<int>(base.y), "and the ResizeEvent is in that size, not the screen's");
    const kuge::Image shot = backend.renderer->readPixels();

    Assert(shot.width > 400, "a screenshot is the pixels of the screen, and says its size (%d)", shot.width);
    AssertEq(static_cast<int>(std::lround(shot.width * base.y / base.x)), shot.height, "of the same ratio");
    backend.window->setFullscreen(false);
    backend.input->poll(events);
    Assert(backend.window->size() == kuge::Vec2(320.0f, 200.0f), "back in a window: the real size");
    backend.window->setFullscreen(true);
    backend.window->setBaseResolution(0);
    Assert(backend.window->size().x > 400.0f, "none: the pixels of the screen");
}
