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

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  Makes the k3::Camera of the scene follow an entity that has a Transform3D (a component)
     *
     * UpdateCamera turns yaw and pitch with the mouse (a mouse look: see IInputSource::setRelativeMouse()),
     * then places the camera. If several entities have one, the one with the lowest number is used.
     *
     *     world.add<kuge::CameraRig>(player, kuge::CameraRig{});                              // first person
     *     world.add<kuge::CameraRig>(player, kuge::CameraRig{.mode = kuge::CameraRig::Mode::Orbit, .distance = 6.0f});
     *
     * The game reads `yaw` to steer the entity the way it looks. There is no roll: k3::Camera has none.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct CameraRig
    {
        enum class Mode {
            FirstPerson,   //!< The camera is at `offset` from the entity, and looks where yaw and pitch say
            Orbit          //!< The camera looks at `offset` from the entity, `distance` away, behind where it looks
        };

        Mode  mode = Mode::FirstPerson;
        k3::Math::Vector3f offset{0.0f, 1.7f, 0.0f};   //!< In the world: the eyes above the feet, or the point looked at
        float distance = 5.0f;                         //!< Orbit only
        float yaw = 0.0f;                              //!< Radians, as k3::Camera: positive turns right
        float pitch = 0.0f;                            //!< Radians, positive looks up
        float sensitivity = 0.0013f;                   //!< Radians per pixel of the mouse (0.075 degrees)
        float maxMove = 250.0f;                        //!< The most pixels of mouse that count in a frame: more is a glitch, not a hand
        float minPitch = -1.5f;                        //!< (a little less than a quarter turn: not straight down)
        float maxPitch = 1.5f;
        bool  look = true;                             //!< Follows the mouse (false: the game sets yaw and pitch)
    };

    //! What a first-person player holds (a component, on an entity of its own: it has no Transform3D): a model
    //! drawn in front of everything, from the camera, with a field of view of its own, so that it never goes
    //! through a wall (kronk3d's "layer"). `offset` is where it is from the eyes: x to the right, y up, -z ahead.
    struct ViewModel3D
    {
        std::shared_ptr<const k3::Model> model;
        k3::Math::Vector3f offset{0.22f, -0.2f, -0.45f};
        float  kick = 0.0f;                     //!< Meters it is pulled back (a recoil: the game moves it)
        float  fovY = 0.9f;                     //!< Radians
        bool   visible = true;
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
