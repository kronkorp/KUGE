#pragma once

#include "Replication.hpp"
#include "PhysicsComponents.hpp"
#include "Transform2D.hpp"

namespace kuge::replication
{

    //! Transform2D: position, rotation, scale. Interpolated between two values (the rotation by the short way).
    //! @param predicted  Part of what a client simulates for its own entity
    inline std::size_t registerTransform2D(ReplicationRegistry& registry, Replicate mode = Replicate::Interpolated, bool predicted = false)
    {
        return registry.component<Transform2D>("Transform2D", mode,
            [](ByteWriter& out, const Transform2D& t) {
                out.write(t.position.x); out.write(t.position.y); out.write(t.rotation); out.write(t.scale.x); out.write(t.scale.y);
            },
            [](ByteReader& in) {
                Transform2D t;

                t.position.x = in.read<float>(); t.position.y = in.read<float>(); t.rotation = in.read<float>();
                t.scale.x = in.read<float>(); t.scale.y = in.read<float>();
                return t;
            },
            [](const Transform2D& a, const Transform2D& b, float k) {
                Transform2D t;

                t.position = Vec2::lerp(a.position, b.position, k);
                t.rotation = lerpAngle(a.rotation, b.rotation, k);
                t.scale = Vec2::lerp(a.scale, b.scale, k);
                return t;
            },
            predicted);
    }

    //! Body: its kind, its velocity, and what it touches (a body that stands on the floor needs to know it)
    inline std::size_t registerBody(ReplicationRegistry& registry, Replicate mode = Replicate::OnChange, bool predicted = false)
    {
        return registry.component<Body>("Body", mode,
            [](ByteWriter& out, const Body& b) {
                out.write(b.type); out.write(b.velocity.x); out.write(b.velocity.y); out.write(b.gravityScale);
                out.write(b.contacts.left); out.write(b.contacts.right); out.write(b.contacts.up); out.write(b.contacts.down);
            },
            [](ByteReader& in) {
                Body b;

                b.type = in.read<Body::Type>(); b.velocity.x = in.read<float>(); b.velocity.y = in.read<float>(); b.gravityScale = in.read<float>();
                b.contacts.left = in.read<bool>(); b.contacts.right = in.read<bool>(); b.contacts.up = in.read<bool>(); b.contacts.down = in.read<bool>();
                return b;
            },
            {}, predicted);
    }

}
