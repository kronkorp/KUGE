#include "Render3DModule.hpp"
#include "Engine.hpp"
#include "Log.hpp"          // kronk3d's (setLogSink)
#include "Logger.hpp"
#include "Particles3D.hpp"
#include "Ref.hpp"
#include <algorithm>
#include <any>
#include <format>
#include <kronk3d/io/GltfLoader.hpp>
#include <kronk3d/io/ObjLoader.hpp>

namespace
{
    // kronk3d's messages go to KUGE's logger: one output, one format. (They would go to its own, which is a
    // copy of KUGE's under other names, see the CMakeLists.txt at the root.)
    void logLikeKuge(k3::LogLevel level, std::string_view message)
    {
        switch (level) {
            case k3::LogLevel::Debug:   Logger::logger().debug("{}", message); break;
            case k3::LogLevel::Info:    Logger::logger().info("{}", message); break;
            case k3::LogLevel::Warning: Logger::logger().warn("{}", message); break;
            case k3::LogLevel::Error:   Logger::logger().error("{}", message); break;
        }
    }

    template<typename T>
    T unwrap(k3::Result<T>&& result, const std::filesystem::path& path)
    {
        if (!result) {
            throw kuge::ModelError(std::format("'{}': {}", path.string(), result.error()));
        }
        return std::move(*result);
    }

    k3::Model readModel(const std::filesystem::path& path)
    {
        return unwrap(k3::ObjLoader::load(path), path);
    }

    k3::AnimatedModel readAnimatedModel(const std::filesystem::path& path)
    {
        return unwrap(k3::GltfLoader::load(path), path);
    }

    // The two halves of a background load: the file is read and decoded on a worker (a model is plain
    // data, it needs no thread in particular), and pump() makes the asset
    template<typename T, T (*Read)(const std::filesystem::path&)>
    void loadInBackground(kuge::AssetManager<T>& assets, kuge::Engine& engine)
    {
        assets.enableAsync(
            [&engine]() -> kuge::ThreadPool& { return engine.pool(); },
            [](const std::filesystem::path& path) -> std::any { return Read(path); },
            [](std::any&& data, const std::filesystem::path&) {
                return std::make_shared<T>(std::any_cast<T>(std::move(data)));
            });
    }
}

kuge::Render3DModule::Render3DModule(Render3DConfig config) : Render3DModule(nullptr, config) {}

kuge::Render3DModule::Render3DModule(ClientModule& client, Render3DConfig config) : Render3DModule(&client, config) {}

kuge::Render3DModule::Render3DModule(ClientModule* client, Render3DConfig config)
    : m_config(config),
      m_models([](const std::filesystem::path& path) {
          return std::make_shared<k3::Model>(readModel(path));
      }, [](k3::Model& model, const std::filesystem::path& path) {
          model = readModel(path);   // read first: it may throw
      }),
      m_animatedModels([](const std::filesystem::path& path) {
          return std::make_shared<k3::AnimatedModel>(readAnimatedModel(path));
      }, [this](k3::AnimatedModel& model, const std::filesystem::path& path) {
          model = readAnimatedModel(path);   // read first: it may throw
          ++m_revision.animated;
      })
{
    k3::setLogSink(logLikeKuge);   // (before anything of kronk3d can log)
    IOpenGL* gl = client ? client->opengl() : nullptr;
    k3::RasterizerConfig rasterizer;

    rasterizer.antiAliasing = config.antiAliasing;
    rasterizer.renderScale = config.renderScale;
    rasterizer.threads = config.threads;
    if (config.rendering == Rendering::OpenGL && !gl) {
        throw Render3DError("the GPU was asked for, and the backend has no OpenGL context (WindowConfig::openGL)");
    }
    if (gl && config.rendering != Rendering::Software) {
        // (the size of the screen: ModelRender follows it afterwards)
        const Vec2 screen = client->renderer().outputSize();

        rasterizer.width = std::max(1u, static_cast<std::uint32_t>(screen.x));
        rasterizer.height = std::max(1u, static_cast<std::uint32_t>(screen.y));
        rasterizer.glLoader = [gl](const char* name) { return reinterpret_cast<k3::GLProc>(gl->procAddress(name)); };
        gl->makeCurrent();
        auto created = k3::createRasterizer(k3::Backend::OpenGL, rasterizer);

        if (created) {
            m_rasterizer = std::move(*created);
            m_gl = gl;
            m_rendererName = gl->description();
            Logger::logger().info("3D: on the GPU, {}", m_rendererName);
            return;
        }
        if (config.rendering == Rendering::OpenGL) {
            throw Render3DError(std::string("cannot start the 3D renderer on the GPU: ") + created.error());
        }
        Logger::logger().warn("3D: not on the GPU ({}), on the CPU", created.error());
    }
    // (its size is the screen's: ModelRender sets it)
    auto created = k3::createRasterizer(k3::Backend::Software, rasterizer);

    if (!created) {
        throw Render3DError(std::string("cannot start the 3D renderer: ") + created.error());
    }
    m_rasterizer = std::move(*created);
}

kuge::Render3DModule::~Render3DModule(void)
{
    if (m_gl) {
        m_gl->makeCurrent();       // (what the GPU holds is freed with its context current)
        m_rasterizer.reset();
    }
}

void kuge::Render3DModule::onAttach(Engine& engine)
{
    loadInBackground<k3::Model, readModel>(m_models, engine);
    loadInBackground<k3::AnimatedModel, readAnimatedModel>(m_animatedModels, engine);
}

void kuge::Render3DModule::inject(kw::World& world)
{
    world.addResource<Ref<k3::IRasterizer>>(*m_rasterizer);
    world.addResource<Ref<AssetManager<k3::Model>>>(m_models);
    world.addResource<Ref<AssetManager<k3::AnimatedModel>>>(m_animatedModels);
    world.addResource<Ref<const ModelRevision>>(m_revision);
    world.addResource<GlContext>(GlContext{m_gl});
    world.addResource<Particles3D>();
    world.addResource<k3::Camera>();
    world.addResource<k3::Environment>();
}

void kuge::Render3DModule::beginFrame(Engine&)
{
    m_models.pump();
    m_animatedModels.pump();
    if (m_config.watchAssets > 0.0) {
        const auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration<double>(now - m_lastWatch).count() >= m_config.watchAssets) {
            m_lastWatch = now;
            reloadAssets();
        }
    }
}

std::size_t kuge::Render3DModule::reloadAssets(void)
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

    tell(m_models.reloadChanged());
    tell(m_animatedModels.reloadChanged());
    return count;
}
