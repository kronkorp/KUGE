#pragma once

#include "PhysicsComponents.hpp"
#include "Shapes.hpp"
#include "SolidGrid.hpp"
#include "SpatialGrid.hpp"
#include "Transform2D.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace kuge
{

    struct PhysicsConfig
    {
        Vec2  gravity{0.0f, 1200.0f};   //!< Pixels per second squared. y points down.
        float maxFallSpeed = 1600.0f;   //!< A falling body does not go faster than that
        float cellSize     = 64.0f;     //!< Of the broad phase: about the size of the biggest body
    };

    //! Something entered or left a trigger
    struct TriggerEvent
    {
        kw::Entity trigger;
        kw::Entity other;
        bool       entered;   //!< false: it left
    };

    struct RaycastHit
    {
        kw::Entity entity{};      //!< What was hit (unless tile)
        bool       tile = false;  //!< A solid tile was hit
        Vec2       point{};
        Vec2       normal{};      //!< Zero if the ray starts inside
        float      distance = 0.0f;
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The 2D physics of a scene (a resource of the World, see
     *         installPhysics())
     *
     * At each tick, in the stage Physics: the bodies get the gravity, move along
     * x then along y, each stopped by the walls; then the triggers are checked.
     * The results (Body::contacts, events()) are those of the tick that just
     * ran, and what the queries answer describes the world as it was left by
     * that tick.
     *
     * It gives the same result for the same world, always: what it does never
     * depends on the order the ECS stores things in, only on the entities.
     */
    ////////////////////////////////////////////////////////////////////////////
    class Physics2D
    {
        public:
            explicit Physics2D(PhysicsConfig config = {});

            PhysicsConfig config;

            //! The solid tiles that stop bodies, like walls
            SolidGrid tiles;

            //! Does one tick. Called by the system installPhysics() adds.
            void step(kw::World& world, float dt);

            //! Who entered or left a trigger during the last tick. Sorted.
            const std::vector<TriggerEvent>& events(void) const noexcept { return m_events; }

            // -- Questions about the world ------------------------------------------------------
            // They only see what has a Collider and a Transform2D. Triggers are left out
            // unless asked for. mask is a set of layers: only what is on one of them counts.

            //! The first thing a ray meets (a solid tile or a collider)
            //! @param direction  any length but 0
            std::optional<RaycastHit> raycast(Vec2 origin, Vec2 direction, float maxDistance,
                std::uint32_t mask = 0xFFFFFFFFu, bool triggers = false) const;

            //! Everything that overlaps the rectangle, by increasing entity
            std::vector<kw::Entity> overlapRect(const Rect& area, std::uint32_t mask = 0xFFFFFFFFu,
                bool triggers = false) const;

            std::vector<kw::Entity> overlapCircle(Vec2 center, float radius, std::uint32_t mask = 0xFFFFFFFFu,
                bool triggers = false) const;

        private:
            struct Item
            {
                kw::Entity entity;
                Collider   collider;
                Aabb       box;
                Circle     circle;     // when collider.shape is a circle
                bool       mover;
            };

            struct Pair
            {
                kw::Entity trigger;
                kw::Entity other;

                bool operator<(const Pair& o) const { return trigger != o.trigger ? trigger < o.trigger : other < o.other; }
                bool operator==(const Pair& o) const { return trigger == o.trigger && other == o.other; }
            };

            struct Mover;

            void collect(kw::World& world);
            void moveBodies(kw::World& world, float dt);
            void moveAlong(Mover& mover, int axis, float delta);
            bool touchesWall(const Mover& mover, const Aabb& box) const;
            void findContacts(Mover& mover) const;
            void findTriggers(kw::World& world);
            static bool touches(const Item& a, const Item& b);

            std::vector<kw::Entity>    m_entities;      // reused between ticks
            std::vector<Item>          m_items;         // sorted by entity
            SpatialGrid                m_solids;        // the walls, before the bodies move
            SpatialGrid                m_all;           // everything, after
            std::vector<Pair>          m_pairs;
            std::vector<Pair>          m_previousPairs;
            std::vector<TriggerEvent>  m_events;
            std::vector<std::uint32_t> m_candidates;    // reused between queries
    };

}
