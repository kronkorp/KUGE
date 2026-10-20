#pragma once

#include "Physics3DComponents.hpp"
#include "Scene.hpp"
#include "Stage.hpp"
#include "Transform3D.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace kuge
{

    struct Physics3DConfig
    {
        k3::Math::Vector3f gravity{0.0f, -9.81f, 0.0f};   //!< Meters per second squared. y points up.
        std::uint32_t      maxBodies = 4096;
        std::uint32_t      maxContacts = 8192;            //!< Contacts between bodies that Jolt can solve in a tick
        std::size_t        tempMemory = 32 * 1024 * 1024; //!< Bytes Jolt works in during a tick (only used as it needs them)
    };

    //! Something entered or left a trigger
    struct TriggerEvent3D
    {
        kw::Entity trigger;
        kw::Entity other;
        bool       entered;   //!< false: it left
    };

    struct RaycastHit3D
    {
        kw::Entity         entity{};
        k3::Math::Vector3f point{};
        k3::Math::Vector3f normal{};
        float              distance = 0.0f;
    };

    //! Which entities a question skips: return true to skip one
    using EntityFilter = std::function<bool(kw::Entity)>;

    //! What a shot hit: the first wall or body, or a part (a Hitbox) of an entity that has Hitboxes
    struct ShotHit3D : RaycastHit3D
    {
        int   hitbox = -1;          //!< The index in Hitboxes::parts of the part that was hit; -1: not a hitbox (a wall, a body)
        float damageScale = 1.0f;   //!< Of that part (1 for a wall)
    };

    //! What a character wants to do during a tick (see Physics3D::moveCharacter())
    struct CharacterMove
    {
        k3::Math::Vector3f wish{};     //!< Meters per second on the ground, x and z (y is ignored: gravity and jumps are the physics')
        bool               jump = false;
        bool               crouch = false;   //!< Held: it stands up when this is false, if there is room
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The 3D physics of a scene (a resource of the World, see installPhysics3D())
     *
     * It is Jolt (rigid bodies, collisions, gravity), run on one thread: the engine's scenes already run
     * side by side. The entities that have a Transform3D and a Collider3D are the bodies, which it makes,
     * moves and removes by itself:
     *
     *     world.add<Collider3D>(floor, Collider3D::box(50, 1, 50));                      // a floor
     *     world.add<Collider3D>(crate, Collider3D::box(1, 1, 1));  world.add<Body3D>(crate, Body3D{});
     *
     * At each tick, in the stage Physics, what the game wrote in a Transform3D or in a Body3D::velocity since
     * the last tick is given to Jolt (a teleport, a walk: a client puts there what the server said, and
     * simulates again), Jolt does the tick, and the Transform3D and the Body3D::velocity of the bodies that
     * move are written. Bodies are made in the order of the entities: the same world gives the same result.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Physics3D
    {
        public:
            explicit Physics3D(Physics3DConfig config = {});
            ~Physics3D(void);

            Physics3D(const Physics3D&)            = delete;
            Physics3D& operator=(const Physics3D&) = delete;

            //! Does one tick. Called by the system installPhysics3D() adds.
            void step(kw::World& world, float dt);

            //! Who entered or left a trigger during the last tick. Sorted.
            const std::vector<TriggerEvent3D>& events(void) const noexcept { return m_events; }

            // -- Questions about the world: what has a Collider3D, as of the last tick. Triggers are left out
            // unless asked for. mask is a set of layers: only what is on one of them counts.

            //! The first thing a ray meets
            //! @param direction  any length but 0
            //! @param ignore  Entities to look through (a wall that lets a shot pass, the shooter); none: all count
            std::optional<RaycastHit3D> raycast(k3::Math::Vector3f origin, k3::Math::Vector3f direction, float maxDistance,
                std::uint32_t mask = 0xFFFFFFFFu, bool triggers = false, const EntityFilter& ignore = {}) const;

            //! Slides a capsule (standing along y, `height` is the whole of it, `origin` its center) along a line until it
            //! touches something: how far it went (`distance`), where, and the normal of what stopped it.
            //! Nothing if the way is free to `maxDistance`. What it starts inside of is not a hit.
            std::optional<RaycastHit3D> sweepCapsule(k3::Math::Vector3f origin, k3::Math::Vector3f direction, float maxDistance,
                float radius, float height, std::uint32_t mask = 0xFFFFFFFFu, bool triggers = false, const EntityFilter& ignore = {}) const;

            //! A hitscan shot: the first thing a ray meets among the walls and bodies **and** the Hitboxes of the entities
            //! of the world (the part that was hit and its damage scale). An entity that has Hitboxes is only hit
            //! through them, not through its Collider3D. Ties go to the lowest entity. Parts are placed by the Transform3D of
            //! their entity (and its bones): the same as when the shooter fired, if the room kept that world (ticket 026).
            std::optional<ShotHit3D> shoot(kw::World& world, k3::Math::Vector3f origin, k3::Math::Vector3f direction, float maxDistance,
                std::uint32_t mask = 0xFFFFFFFFu, const EntityFilter& ignore = {}) const;

            //! Moves a character (an entity with a Transform3D and a Character3D): walks it by the wish, slides it along
            //! what it hits, climbs a step, sticks it to a slope, lets it jump and crouch, and falls it. Call it once
            //! per tick, in the Simulation stage, **with the same inputs on the client and in the room**. It sees the
            //! world as the last Physics3D::step() left it. The Transform3D and the Character3D are written.
            void moveCharacter(kw::World& world, kw::Entity entity, const CharacterMove& move, float dt);

            //! Everything that overlaps the sphere, by increasing entity
            std::vector<kw::Entity> overlapSphere(k3::Math::Vector3f center, float radius, std::uint32_t mask = 0xFFFFFFFFu,
                bool triggers = false) const;

            //! Bodies that Jolt has now (to see that they come and go with the entities)
            std::size_t bodyCount(void) const noexcept;

        private:
            struct Impl;

            std::unique_ptr<Impl>       m_impl;
            std::vector<TriggerEvent3D> m_events;
    };

    //! Fixed, stage Physics: does one tick of the Physics3D of the World
    class Physics3DStep : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;
    };

    //! Gives a scene its 3D physics: the Physics3D resource, and the system that runs it at each tick
    //! between the Simulation and Late stages.
    //!
    //!     void Level::onEnter() {
    //!         kuge::installPhysics3D(setup(), {.gravity = {0, -20, 0}});
    //!     }
    void installPhysics3D(SceneSetup scene, Physics3DConfig config = {});

}
