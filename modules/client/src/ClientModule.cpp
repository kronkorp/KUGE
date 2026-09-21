#include "ClientModule.hpp"
#include "Engine.hpp"
#include "Ref.hpp"
#include "input/ActionState.hpp"
#include "render/Components.hpp"
#include <stdexcept>

namespace
{
    kuge::Backend checked(kuge::Backend backend)
    {
        if (!backend.window || !backend.input || !backend.renderer) {
            throw std::invalid_argument("ClientModule: the backend needs a window, inputs and a renderer");
        }
        return backend;
    }
}

kuge::ClientModule::ClientModule(Backend backend, ClientConfig config)
    : m_backend(checked(std::move(backend))),
      m_config(config),
      m_textures([this](const std::filesystem::path& path) {
          return Texture::fromFile(*m_backend.renderer, path);
      })
{
    const std::uint8_t pixel[4] = {255, 255, 255, 255};

    m_white = Texture::fromPixels(*m_backend.renderer, 1, 1, pixel);
}

void kuge::ClientModule::onAttach(Engine& engine)
{
    if (engine.config().mode != Engine::Mode::Windowed) {
        throw std::logic_error("ClientModule needs an Engine in Mode::Windowed");
    }
}

void kuge::ClientModule::inject(kw::World& world)
{
    world.addResource<Ref<IWindow>>(*m_backend.window);
    world.addResource<Ref<IRenderer2D>>(*m_backend.renderer);
    world.addResource<Ref<InputMap>>(m_input);
    world.addResource<Ref<AssetManager<Texture>>>(m_textures);
    world.addResource<WhitePixel>(WhitePixel{m_white});
    world.addResource<ActionState>();
    world.addResource<Camera2D>();
}

void kuge::ClientModule::beginFrame(Engine& engine)
{
    m_events.clear();
    m_backend.input->poll(m_events);
    for (const Event& event : m_events) {
        if (std::holds_alternative<QuitEvent>(event)) {
            engine.stop();
        }
        m_input.handle(event);
    }
    m_backend.renderer->begin(m_config.clearColor);
}

void kuge::ClientModule::endFrame(Engine&)
{
    if (m_screenshotRequested) {
        m_screenshotRequested = false;
        m_screenshot = m_backend.renderer->readPixels();
    }
    m_backend.renderer->present();
}

std::optional<kuge::Image> kuge::ClientModule::takeScreenshot(void)
{
    std::optional<Image> taken = std::move(m_screenshot);

    m_screenshot.reset();
    return taken;
}
