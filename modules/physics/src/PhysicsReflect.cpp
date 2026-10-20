#include "PhysicsReflect.hpp"
#include "PhysicsComponents.hpp"

void kuge::describePhysics(ComponentRegistry& registry)
{
    registry.describe<Collider>("Collider", {
        field<Collider>("shape", &Collider::shape, {.names = {"Box", "Circle"}}),
        field<Collider>("size", &Collider::size, {.help = "Box: width and height. Circle: x is the radius"}),
        field<Collider>("offset", &Collider::offset),
        field<Collider>("layer", &Collider::layer),
        field<Collider>("mask", &Collider::mask),
        field<Collider>("trigger", &Collider::trigger),
    });
    registry.describe<Body>("Body", {
        field<Body>("type", &Body::type, {.names = {"Static", "Kinematic", "Dynamic"}}),
        field<Body>("velocity", &Body::velocity),
        field<Body>("gravityScale", &Body::gravityScale),
        field<Body>("restitution", &Body::restitution, {.min = 0.0, .max = 1.0}),
    });
}
