#include "Render3DModule.hpp"
#include "Engine.hpp"
#include "Logger.hpp"
#include "Ref.hpp"
#include <any>
#include <format>
#include <kronk3d/io/GltfLoader.hpp>
#include <kronk3d/io/ObjLoader.hpp>

namespace
{
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

kuge::Render3DModule::Render3DModule(Render3DConfig config)
    : m_config(config),
      m_models([](const std::filesystem::path& path) {
          return std::make_shared<k3::Model>(readModel(path));
      }, [](k3::Model& model, const std::filesystem::path& path) {
          model = readModel(path);   // read first: it may throw
      }),
      m_animatedModels([](const std::filesystem::path& path) {
          return std::make_shared<k3::AnimatedModel>(readAnimatedModel(path));
      })
{
    k3::RasterizerConfig rasterizer;

    rasterizer.antiAliasing = config.antiAliasing;
    rasterizer.renderScale = config.renderScale;
    rasterizer.threads = config.threads;
    // (its size is the screen's: ModelRender sets it)
    auto created = k3::createRasterizer(k3::Backend::Software, rasterizer);

    if (!created) {
        throw Render3DError(std::string("cannot start the 3D renderer: ") + created.error());
    }
    m_rasterizer = std::move(*created);
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
    const auto report = m_models.reloadChanged();

    for (const auto& [path, why] : report.failed) {
        Logger::logger().warn("reload: '{}' kept as it was: {}", path.string(), why);
    }
    for (const auto& path : report.reloaded) {
        Logger::logger().info("reload: '{}'", path.string());
    }
    return report.reloaded.size();
}
