#pragma once

#include "Replication.hpp"
#include "AnimationState3D.hpp"
#include "Physics3DComponents.hpp"
#include "PhysicsComponents.hpp"
#include "Transform2D.hpp"
#include "Transform3D.hpp"

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

    //! Transform3D: position, rotation (a quaternion), scale. Interpolated between two values (the rotation by the
    //! short way, see lerp()).
    //! @param predicted  Part of what a client simulates for its own entity
    inline std::size_t registerTransform3D(ReplicationRegistry& registry, Replicate mode = Replicate::Interpolated, bool predicted = false)
    {
        return registry.component<Transform3D>("Transform3D", mode,
            [](ByteWriter& out, const Transform3D& t) {
                out.write(t.translation.x); out.write(t.translation.y); out.write(t.translation.z);
                out.write(t.rotation.x); out.write(t.rotation.y); out.write(t.rotation.z); out.write(t.rotation.w);
                out.write(t.scale.x); out.write(t.scale.y); out.write(t.scale.z);
            },
            [](ByteReader& in) {
                Transform3D t;

                t.translation.x = in.read<float>(); t.translation.y = in.read<float>(); t.translation.z = in.read<float>();
                t.rotation.x = in.read<float>(); t.rotation.y = in.read<float>(); t.rotation.z = in.read<float>();
                t.rotation.w = in.read<float>();
                t.scale.x = in.read<float>(); t.scale.y = in.read<float>(); t.scale.z = in.read<float>();
                return t;
            },
            [](const Transform3D& a, const Transform3D& b, float k) { return lerp(a, b, k); },
            predicted);
    }

    //! Body3D: its kind, its velocity, and the rest of what the physics needs to simulate it again
    //! @param predicted  Part of what a client simulates for its own entity
    inline std::size_t registerBody3D(ReplicationRegistry& registry, Replicate mode = Replicate::OnChange, bool predicted = false)
    {
        return registry.component<Body3D>("Body3D", mode,
            [](ByteWriter& out, const Body3D& b) {
                out.write(b.type); out.write(b.velocity.x); out.write(b.velocity.y); out.write(b.velocity.z);
                out.write(b.gravityScale); out.write(b.mass); out.write(b.lockRotation); out.write(b.grounded);
            },
            [](ByteReader& in) {
                Body3D b;

                b.type = in.read<Body3D::Type>();
                b.velocity.x = in.read<float>(); b.velocity.y = in.read<float>(); b.velocity.z = in.read<float>();
                b.gravityScale = in.read<float>(); b.mass = in.read<float>();
                b.lockRotation = in.read<bool>(); b.grounded = in.read<bool>();
                return b;
            },
            {}, predicted);
    }

    //! AnimationState3D: the clip of a character. Sent when it changes; the client plays it (see PlayReplicatedClips).
    inline std::size_t registerAnimationState3D(ReplicationRegistry& registry, Replicate mode = Replicate::OnChange)
    {
        return registry.component<AnimationState3D>("AnimationState3D", mode,
            [](ByteWriter& out, const AnimationState3D& s) { out.writeString(s.clip); out.write(s.loop); out.write(s.serial); },
            [](ByteReader& in) {
                AnimationState3D s;

                s.clip = in.readString();
                s.loop = in.read<bool>();
                s.serial = in.read<std::uint8_t>();
                return s;
            });
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
