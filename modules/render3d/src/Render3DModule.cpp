#include "Render3DModule.hpp"
#include "Ref.hpp"

kuge::Render3DModule::Render3DModule(Render3DConfig config)
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

void kuge::Render3DModule::inject(kw::World& world)
{
    world.addResource<Ref<k3::IRasterizer>>(*m_rasterizer);
    world.addResource<k3::Camera>();
    world.addResource<k3::Environment>();
}
