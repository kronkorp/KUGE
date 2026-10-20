#include "Particles3D.hpp"
#include <algorithm>
#include <cmath>
#include <kronk3d/scene/Image.hpp>
#include <kronk3d/scene/Texture.hpp>

kuge::Particles3D::Particles3D(void) : m_material(std::make_shared<k3::Material>())
{
    // A soft round dot: white, opaque in the middle, clear at the edge
    constexpr std::uint32_t SIZE = 16;
    k3::Image dot(SIZE, SIZE, k3::Math::Color{1.0f, 1.0f, 1.0f, 0.0f});

    for (std::uint32_t y = 0; y < SIZE; ++y) {
        for (std::uint32_t x = 0; x < SIZE; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f) / SIZE * 2.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / SIZE * 2.0f - 1.0f;
            const float edge = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            std::uint8_t* texel = dot.texel(x, y);

            texel[0] = texel[1] = texel[2] = 255;
            texel[3] = static_cast<std::uint8_t>(255.0f * std::min(1.0f, edge * 2.0f));
        }
    }
    m_material->diffuseMap = std::make_shared<k3::Texture>(std::move(dot));
    m_material->alphaMode = k3::AlphaMode::Blend;
    m_material->unlit = true;
    m_material->doubleSided = true;
    m_material->castShadows = false;
}

void kuge::Particles3D::burst(const Burst& burst)
{
    const float length = k3::Math::Vector3f::length(burst.direction);

    for (int i = 0; i < burst.count; ++i) {
        // A direction: every way, or a cone around the one given
        k3::Math::Vector3f way{unit() * 2.0f - 1.0f, unit() * 2.0f - 1.0f, unit() * 2.0f - 1.0f};

        way = k3::Math::Vector3f::normalize(way);
        if (length > 0.0f) {
            const k3::Math::Vector3f axis = burst.direction / length;

            way = k3::Math::Vector3f::normalize(axis * 1.2f + way * 0.5f);
        }
        const float share = 0.3f + 0.7f * unit();
        Particle particle;

        particle.position = burst.at;
        particle.velocity = way * (burst.speed * share);
        particle.life = std::max(0.01f, burst.life * (0.4f + 0.6f * unit()));
        particle.size = burst.size * (0.6f + 0.4f * unit());
        particle.gravity = burst.gravity;
        particle.color = burst.color;
        m_particles.push_back(particle);
    }
    if (m_particles.size() > m_limit) {
        m_particles.erase(m_particles.begin(), m_particles.begin() + static_cast<std::ptrdiff_t>(m_particles.size() - m_limit));
    }
}

void kuge::Particles3D::step(float seconds)
{
    for (Particle& particle : m_particles) {
        particle.age += seconds;
        particle.velocity.y -= particle.gravity * seconds;
        particle.position = particle.position + particle.velocity * seconds;
    }
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(), [](const Particle& p) { return p.age >= p.life; }), m_particles.end());
}

void kuge::Particles3D::draw(k3::IRasterizer& rasterizer, const k3::Camera& camera)
{
    m_batch.clear();
    for (const Particle& particle : m_particles) {
        k3::Billboard billboard;
        const float fade = 1.0f - particle.age / particle.life;

        billboard.position = particle.position;
        billboard.size = {particle.size, particle.size};
        billboard.color = {particle.color.r, particle.color.g, particle.color.b, particle.color.a * fade};
        m_batch.add(billboard);
    }
    m_batch.draw(rasterizer, camera, m_material);
}
