#pragma once

#include <kronk3d/render/Environment.hpp>
#include <kronk3d/scene/Light.hpp>
#include <kronk3d/scene/Model.hpp>
#include <cstdint>
#include <memory>

namespace kuge
{

    //! What to draw for an entity that has a Transform3D: a kronk3d model (meshes and materials), shared
    //! between the entities that show the same one
    struct Model3D
    {
        std::shared_ptr<const k3::Model> model;
        bool visible = true;
    };

    //! A light that follows its entity: with a Transform3D, `light.position` and `light.direction` are
    //! in the space of the entity (a point light at {0, 2, 0} hangs two meters above it); without one,
    //! they are in the world. At most k3::MAX_LIGHTS lights are lit (the ones of the Environment first).
    struct Light3D
    {
        k3::Light light{};
    };

    //! How many times a glTF model was reloaded in place (a resource of the World): an Animator3D made
    //! before starts again from its model when this moves
    struct ModelRevision
    {
        std::uint64_t animated = 0;
    };

    // What the scene shows is a k3::Camera and a k3::Environment (fog, sky, ambient light, shadows...):
    // resources of the World, like Camera2D is.

}
