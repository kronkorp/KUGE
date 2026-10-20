#include "Physics.hpp"
#include "DebugDraw.hpp"
#include "Time.hpp"

bool kuge::PhysicsStep::handle(kw::World& world)
{
    world.getResource<Physics2D>().step(world, static_cast<float>(world.getResource<Time>().dt));
    // What the developer asked to see: the colliders, where the tick left them (nothing is computed when it is off)
    DebugDraw& debug = world.getResource<DebugDraw>();

    if (debug.colliders) {
        for (kw::Entity entity : world.view<Transform2D, Collider>()) {
            const Collider& collider = world.get<Collider>(entity);
            DebugShape shape;
            Body::Type type = Body::Type::Static;

            if (world.has<Body>(entity)) {
                type = world.get<Body>(entity).type;
            }
            shape.form = collider.shape == Collider::Shape::Circle ? DebugShape::Form::Circle : DebugShape::Form::Box;
            shape.center = world.get<Transform2D>(entity).position + collider.offset;
            shape.size = collider.size;
            shape.kind = collider.trigger ? DebugKind::Sensor : (type == Body::Type::Static ? DebugKind::Static : DebugKind::Dynamic);
            debug.shapes.push_back(shape);
        }
    }
    return true;
}

void kuge::installPhysics(SceneSetup scene, PhysicsConfig config)
{
    scene.world().addResource<Physics2D>(config);
    scene.addSystem(kw::Schedule::Fixed, stage::Physics, std::make_unique<PhysicsStep>());
}
