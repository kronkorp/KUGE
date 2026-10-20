extern "C" {
    #include "kronklab/kronklab.h"
}
#include "backend/SdlBackend.hpp"
#include "client_fixture.hpp"
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
