#pragma once

#include <kronk3d/math/Color.hpp>
#include <kronk3d/math/Vector.hpp>
#include <kronk3d/render/IRasterizer.hpp>
#include <kronk3d/scene/BillboardBatch.hpp>
#include <kronk3d/scene/Camera.hpp>
#include <memory>
#include <random>
#include <vector>

namespace kuge
{

    //! A puff of particles: sparks, smoke, dust. Fields have the values of a spark.
    struct Burst
    {
        k3::Math::Vector3f at{};
        int                count = 12;
        float              speed = 3.0f;                 //!< Meters per second, at the most (each takes a share of it)
        float              life = 0.4f;                  //!< Seconds, at the most (each takes a share of it)
        float              size = 0.08f;                 //!< Meters across
        float              gravity = 8.0f;               //!< Meters per second squared, down
        k3::Math::Color    color{1.0f, 0.75f, 0.3f, 1.0f};
        //! Without a direction the particles go every way; with one (a unit vector) they go in a cone of about 40 degrees around it
        k3::Math::Vector3f direction{};
    };

    ////////////////////////////////////////////////////////////////////////////
    /**
     * @brief  The particles of a scene (a resource of the World): ModelRender moves them and draws them, as soft round
     *         quads that face the camera (kronk3d's BillboardBatch), and they fade out as they age
     *
     *     world.getResource<kuge::Particles3D>().burst({.at = hit.point, .count = 20, .color = {1, 0.9, 0.5, 1}});
     */
    ////////////////////////////////////////////////////////////////////////////
    class Particles3D
    {
        public:
            Particles3D(void);

            void burst(const Burst& burst);

            //! Moves them, and lets the old ones go. Called by ModelRender at each frame.
            void step(float seconds);

            //! Draws them in the current frame of the rasterizer. Called by ModelRender.
            void draw(k3::IRasterizer& rasterizer, const k3::Camera& camera);

            std::size_t count(void) const noexcept { return m_particles.size(); }

            //! No more than this many at once (the oldest are dropped): default 2000
            void setLimit(std::size_t limit) noexcept { m_limit = limit; }

        private:
            struct Particle
            {
                k3::Math::Vector3f position;
                k3::Math::Vector3f velocity;
                float              age = 0.0f;
                float              life = 0.0f;
                float              size = 0.0f;
                float              gravity = 0.0f;
                k3::Math::Color    color;
            };

            float unit(void) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_random); }

            std::vector<Particle>             m_particles;
            std::size_t                       m_limit = 2000;
            std::mt19937                      m_random{7};
            k3::BillboardBatch                m_batch;
            std::shared_ptr<k3::Material>     m_material;
    };

}
