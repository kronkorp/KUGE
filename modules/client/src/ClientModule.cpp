#include "ClientModule.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include "Ref.hpp"
#include "animation/Animation.hpp"
#include "audio/DummyAudio.hpp"
#include "input/ActionState.hpp"
#include "render/Components.hpp"
#include "render/ImageDecoder.hpp"
#include <stdexcept>

namespace
{
    kuge::Backend checked(kuge::Backend backend)
    {
        if (!backend.window || !backend.input || !backend.renderer) {
            throw std::invalid_argument("ClientModule: the backend needs a window, inputs and a renderer");
        }
        if (!backend.audio) {
            backend.audio = std::make_unique<kuge::DummyAudio>();   // no sound: the game just does not make any
        }
        return backend;
    }
}

kuge::ClientModule::ClientModule(Backend backend, ClientConfig config)
    : m_backend(checked(std::move(backend))),
      m_config(config),
      m_textures([this](const std::filesystem::path& path) {
          return Texture::fromFile(*m_backend.renderer, path);
      }, [](Texture& texture, const std::filesystem::path& path) {
          texture.reloadFromFile(path);
      }),
      m_mixer(*m_backend.audio),
      m_sounds([this](const std::filesystem::path& path) {
          return std::make_shared<Sound>(*m_backend.audio, m_backend.audio->loadSound(path));
      }, [this](Sound& sound, const std::filesystem::path& path) {
          sound.replace(m_backend.audio->loadSound(path));   // loaded first: it may throw
      }),
      m_musics([this](const std::filesystem::path& path) {
          return std::make_shared<Music>(*m_backend.audio, m_backend.audio->loadMusic(path));
      }, [this](Music& music, const std::filesystem::path& path) {
          music.replace(m_backend.audio->loadMusic(path));
      }),
      m_text(*m_backend.renderer)
{
    const std::uint8_t pixel[4] = {255, 255, 255, 255};

    m_white = Texture::fromPixels(*m_backend.renderer, 1, 1, pixel);
}

void kuge::ClientModule::onAttach(Engine& engine)
{
    if (engine.config().mode != Engine::Mode::Windowed) {
        throw std::logic_error("ClientModule needs an Engine in Mode::Windowed");
    }
    // A picture is read and decoded on a worker, and made into a texture here (by pump(), in
    // beginFrame()): a texture belongs to the thread of the renderer
    m_textures.enableAsync(
        [&engine]() -> ThreadPool& { return engine.pool(); },
        [](const std::filesystem::path& path) -> std::any { return decodeImageFile(path); },
        [this](std::any&& data, const std::filesystem::path&) {
            const Image image = std::any_cast<Image>(std::move(data));

            return Texture::fromPixels(*m_backend.renderer, image.width, image.height, image.rgba);
        });
}

void kuge::ClientModule::inject(kw::World& world)
{
    world.addResource<Ref<IWindow>>(*m_backend.window);
    world.addResource<Ref<IRenderer2D>>(*m_backend.renderer);
    world.addResource<Ref<InputMap>>(m_input);
    world.addResource<Ref<AssetManager<Texture>>>(m_textures);
    world.addResource<Ref<Audio>>(m_mixer);
    world.addResource<Ref<AssetManager<Sound>>>(m_sounds);
    world.addResource<Ref<AssetManager<Music>>>(m_musics);
    world.addResource<Ref<TextRenderer>>(m_text);
    world.addResource<WhitePixel>(WhitePixel{m_white});
    world.addResource<ActionState>();
    world.addResource<Camera2D>();
    world.addResource<AnimationEvents>();
}

void kuge::ClientModule::beginFrame(Engine& engine)
{
    m_textures.pump();
    m_events.clear();
    m_backend.input->poll(m_events);
    for (const Event& event : m_events) {
        if (std::holds_alternative<QuitEvent>(event)) {
            engine.stop();
        }
        m_input.handle(event);
    }
    if (m_config.watchAssets > 0.0) {
        const auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration<double>(now - m_lastWatch).count() >= m_config.watchAssets) {
            m_lastWatch = now;
            reloadAssets();
        }
    }
    m_text.beginFrame();
    m_backend.renderer->begin(m_config.clearColor);
}

void kuge::ClientModule::endFrame(Engine&)
{
    if (m_screenshotRequested) {
        m_screenshotRequested = false;
        m_screenshot = m_backend.renderer->readPixels();
    }
    m_backend.renderer->present();
    m_mixer.update();
}

std::size_t kuge::ClientModule::reloadAssets(void)
{
    std::size_t count = 0;
    const auto tell = [&count](const auto& report) {
        count += report.reloaded.size();
        for (const auto& [path, why] : report.failed) {
            Logger::logger().warn("reload: '{}' kept as it was: {}", path.string(), why);
        }
        for (const auto& path : report.reloaded) {
            Logger::logger().info("reload: '{}'", path.string());
        }
    };

    tell(m_textures.reloadChanged());
    tell(m_sounds.reloadChanged());
    tell(m_musics.reloadChanged());
    return count;
}

std::optional<kuge::Image> kuge::ClientModule::takeScreenshot(void)
{
    std::optional<Image> taken = std::move(m_screenshot);

    m_screenshot.reset();
    return taken;
}

std::shared_ptr<kuge::IFont> kuge::ClientModule::loadFont(const std::filesystem::path& file, int pointSize)
{
    const auto key = std::make_pair(file.lexically_normal().string(), pointSize);

    if (!m_backend.fonts) {
        throw FontError("this backend has no fonts");
    }
    if (auto known = m_fonts[key].lock()) {
        return known;
    }
    std::shared_ptr<IFont> font = m_backend.fonts->load(file, pointSize);

    m_fonts[key] = font;
    return font;
}
