#include "DebugDraw.hpp"
#include "Stage.hpp"

kuge::DebugCvars kuge::registerDebugCvars(Cvars& cvars)
{
    return {
        cvars.add("debug_colliders", false, CvarFlag::None, "Draw the shape of every collider (blue: static, green: moving, yellow: trigger)"),
        cvars.add("debug_bounds", false, CvarFlag::None, "Draw the box of every sprite"),
        cvars.add("debug_camera", false, CvarFlag::None, "Draw the edge of what the camera shows"),
    };
}

namespace
{
    class ApplyDebug final : public kw::ISystem
    {
        public:
            explicit ApplyDebug(kuge::DebugCvars cvars) : m_cvars(cvars) {}

            bool handle(kw::World& world) override
            {
                auto& debug = world.getResource<kuge::DebugDraw>();

                debug.colliders = m_cvars.colliders.get();
                debug.bounds = m_cvars.bounds.get();
                debug.camera = m_cvars.camera.get();
                debug.shapes.clear();
                return true;
            }

        private:
            kuge::DebugCvars m_cvars;
    };
}

void kuge::installDebug(SceneSetup scene, DebugCvars cvars)
{
    scene.addSystem(kw::Schedule::Fixed, stage::Input, std::make_unique<ApplyDebug>(cvars));
}
