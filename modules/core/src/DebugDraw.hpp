#pragma once

#include "Commands.hpp"
#include "Math2D.hpp"
#include "Scene.hpp"
#include <vector>

namespace kuge
{

    //! What the debug drawing is asked to show of a shape, without the core knowing about colours (the client has them)
    enum class DebugKind : std::uint8_t { Static, Dynamic, Sensor, Other };

    struct DebugShape
    {
        enum class Form : std::uint8_t { Box, Circle };

        Form      form = Form::Box;
        Vec2      center{};
        Vec2      size{};     //!< Box: width and height. Circle: size.x is the radius.
        DebugKind kind = DebugKind::Other;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  What the developer asked to see, and the shapes the systems of the scene filled in for it (a resource of every scene)
     *
     * The flags come from the variables `debug_colliders`, `debug_bounds` and `debug_camera` (installDebug()). When they are off
     * nothing is computed and nothing is drawn: the systems that fill `shapes` test `colliders` first. The shapes are in the
     * coordinates of the world (2D) and are drawn by the client (DebugRender) over the sprites.
     */
    ////////////////////////////////////////////////////////////////////////////
    struct DebugDraw
    {
        bool                    colliders = false;   //!< Fill `shapes` with the colliders (kuge-physics does)
        bool                    bounds = false;      //!< The box of every Sprite (the client draws it)
        bool                    camera = false;      //!< The area the camera shows
        std::vector<DebugShape> shapes;              //!< Emptied at the start of each tick
    };

    //! The variables that switch the drawing on, made by registerDebugCvars()
    struct DebugCvars
    {
        Cvar<bool> colliders, bounds, camera;
    };

    //! `debug_colliders`, `debug_bounds`, `debug_camera` (all off)
    DebugCvars registerDebugCvars(Cvars& cvars);

    //! Gives the scene the system that copies the variables to its DebugDraw at the start of each tick (Fixed, Input) and empties
    //! the shapes. The variables must outlive the scene.
    void installDebug(SceneSetup scene, DebugCvars cvars);

}
