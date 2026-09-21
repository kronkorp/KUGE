#pragma once

#include "Physics2D.hpp"
#include "PhysicsComponents.hpp"
#include "Scene.hpp"
#include "Stage.hpp"

namespace kuge
{

    //! Fixed, stage Physics: does one tick of the Physics2D of the World
    class PhysicsStep : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;
    };

    //! Gives a scene its physics: the Physics2D resource, and the system that
    //! runs it at each tick between the Simulation and Late stages.
    //!
    //!     void Level::onEnter() {
    //!         kuge::installPhysics(setup(), {.gravity = {0, 900}});
    //!         world().getResource<kuge::Physics2D>().tiles.reset(40, 20, 16);
    //!     }
    void installPhysics(SceneSetup scene, PhysicsConfig config = {});

}
