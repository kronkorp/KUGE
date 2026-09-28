#pragma once

#include "backend/IRenderer2D.hpp"
#include "kronkworld/Kronkworld.hpp"
#include <vector>

namespace kuge
{

    //! Fixed, stage Input: gives the ActionState of the tick to the simulation
    //! (a resource of the World) from what the player did since the last tick
    class SampleInput : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;
    };

    //! Fixed, stage Input (after SampleInput): remembers where the entities that
    //! have a PreviousTransform2D were before the simulation moves them
    class SnapshotTransforms : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;
    };

    //! Frame, stage Render: draws the entities that have a Transform2D and a
    //! Sprite or a TilemapView, through the Camera2D. Lowest layer first, and
    //! always in the same order for the same scene, whatever order the World
    //! holds them in.
    class SpriteRender : public kw::ISystem
    {
        public:
            bool handle(kw::World& world) override;

        private:
            struct Item {
                int         layer;
                float       z;
                TextureId   texture;
                kw::Entity  entity;
                TextureDraw draw;
                bool        plain;   //!< A rectangle with no texture
                bool        tiles;   //!< A layer of a tilemap: the entity says which
            };

            void drawTiles(kw::World& world, kw::Entity entity, IRenderer2D& renderer);

            std::vector<Item>  m_items;    //!< Kept between frames: no allocation each time
            std::vector<float> m_columns;  //!< Screen edges of the columns of a tilemap
            std::vector<float> m_rows;
    };

}
