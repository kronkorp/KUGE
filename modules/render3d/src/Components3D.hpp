#pragma once

#include "backend/IOpenGL.hpp"
#include "backend/IRenderer2D.hpp"
#include <functional>
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
     * The game reads `yaw` to steer the entity the way it looks (`punchYaw` and `punchPitch` move the view without moving that aim). `roll` leans the view (a slide, a hit, a dive); the mouse never writes it.
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
        //! An offset of the view, apart from the aim: a recoil, a flinch, a shake. Radians, 0 by default. `UpdateCamera` adds them to
        //! the camera after the clamp of `pitch` (so near `maxPitch` the view goes over it by the punch, and the aim stays at the limit),
        //! and the mouse never writes them: the game sets them each frame, and `yaw` and `pitch` stay what the game sends as its aim.
        float punchYaw = 0.0f;
        float punchPitch = 0.0f;
        float roll = 0.0f;                             //!< Radians around the view direction, positive leans the head to the right (see k3::Camera::roll)
        float sensitivity = 0.0013f;                   //!< Radians per pixel of the mouse (0.075 degrees)
        float maxMove = 250.0f;                        //!< The most pixels of mouse that count in a frame: more is a glitch, not a hand
        float minPitch = -1.5f;                        //!< (a little less than a quarter turn: not straight down)
        float maxPitch = 1.5f;
        bool  look = true;                             //!< Follows the mouse (false: the game sets yaw and pitch)
        //! Orbit only: how far along a ray the camera could go before it meets a wall (negative: nothing is there), so that it
        //! stops short of it (`margin` meters before) instead of going through. A physics' raycast does that:
        //!     rig.obstacle = [&](Vec3 from, Vec3 way, float most) { auto hit = physics.raycast(from, way, most); return hit ? hit->distance : -1.0f; };
        std::function<float(k3::Math::Vector3f from, k3::Math::Vector3f direction, float maxDistance)> obstacle;
        float margin = 0.25f;
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

    //! The OpenGL context when the GPU draws (a resource of the World, null when the CPU does): ModelRender makes it current
    //! before it draws, since the 2D renderer may have taken the context of the thread, and gives it the size of the screen
    struct GlContext
    {
        IOpenGL* gl = nullptr;
    };

    //! How ModelRender enlarges the picture of the scene when it is drawn smaller than the screen (Render3DConfig::upscale), a resource of the World
    struct SceneUpscale
    {
        Filter filter = Filter::Nearest;
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
