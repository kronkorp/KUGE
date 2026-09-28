#include "Physics.hpp"
#include "Time.hpp"

bool kuge::PhysicsStep::handle(kw::World& world)
{
    world.getResource<Physics2D>().step(world, static_cast<float>(world.getResource<Time>().dt));
    return true;
}

void kuge::installPhysics(SceneSetup scene, PhysicsConfig config)
{
    scene.world().addResource<Physics2D>(config);
    scene.addSystem(kw::Schedule::Fixed, stage::Physics, std::make_unique<PhysicsStep>());
}
