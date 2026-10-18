#include "Arena.hpp"

namespace arena
{

    namespace
    {
        kw::Entity solid(kw::World& world, k3::Math::Vector3f at, kuge::Collider3D collider)
        {
            const kw::Entity entity = world.create();
            kuge::Transform3D transform;

            transform.translation = at;
            world.add<kuge::Transform3D>(entity, transform);
            world.add<kuge::Collider3D>(entity, collider);
            return entity;
        }
    }

    void buildLevel(kw::World& world)
    {
        constexpr float WALL = 3.0f;        // high
        constexpr float EDGE = HALF_SIZE + 0.5f;

        world.addResource<kuge::Physics3D>();
        solid(world, {0.0f, -0.5f, 0.0f}, kuge::Collider3D::box(2.0f * HALF_SIZE + 2.0f, 1.0f, 2.0f * HALF_SIZE + 2.0f));
        solid(world, {0.0f, WALL / 2, -EDGE}, kuge::Collider3D::box(2.0f * HALF_SIZE + 2.0f, WALL, 1.0f));
        solid(world, {0.0f, WALL / 2, EDGE}, kuge::Collider3D::box(2.0f * HALF_SIZE + 2.0f, WALL, 1.0f));
        solid(world, {-EDGE, WALL / 2, 0.0f}, kuge::Collider3D::box(1.0f, WALL, 2.0f * HALF_SIZE + 2.0f));
        solid(world, {EDGE, WALL / 2, 0.0f}, kuge::Collider3D::box(1.0f, WALL, 2.0f * HALF_SIZE + 2.0f));
    }

    kw::Entity buildPlayer(kw::World& world, k3::Math::Vector3f at)
    {
        kuge::Body3D body;

        body.lockRotation = true;
        body.mass = 70.0f;
        const kw::Entity player = solid(world, at, kuge::Collider3D::capsule(PLAYER_RADIUS, PLAYER_HEIGHT));

        world.add<kuge::Body3D>(player, body);
        return player;
    }

    void steerPlayer(kw::World& world, kw::Entity player, const Steer& steer)
    {
        kuge::Body3D& body = world.get<kuge::Body3D>(player);
        const float yaw = static_cast<float>(steer.yaw) / 1000.0f;
        // As k3::Camera: at yaw 0 it looks toward -z, and positive turns right
        const float ahead = static_cast<float>(steer.forward) * WALK_SPEED;
        const float side = static_cast<float>(steer.strafe) * WALK_SPEED;

        body.velocity.x = std::sin(yaw) * ahead + std::cos(yaw) * side;
        body.velocity.z = -std::cos(yaw) * ahead + std::sin(yaw) * side;
        if (steer.jump && body.grounded) {
            body.velocity.y = JUMP_SPEED;
        }
    }

    void buildGame(kw::World& world)
    {
        Match& match = world.getResource<Match>();
        const auto coin = [&](k3::Math::Vector3f at) {
            const kw::Entity entity = solid(world, at, kuge::Collider3D::sphere(0.5f).asTrigger());

            world.add<Coin>(entity, Coin{});
            match.track(entity, COIN, 0);
        };

        coin({0.0f, 1.0f, 3.0f});      // right ahead of where the players start (and look)
        for (std::size_t i = 0; i + 1 < COIN_COUNT; ++i) {
            const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(COIN_COUNT - 1);

            coin({std::sin(angle) * 9.0f, 1.0f, std::cos(angle) * 9.0f});
        }
        for (int i = 0; i < 6; ++i) {
            const kw::Entity crate = solid(world, {-6.0f + 2.4f * static_cast<float>(i % 3), 0.5f + static_cast<float>(i / 3), -5.0f},
                kuge::Collider3D::box(1.0f, 1.0f, 1.0f));

            world.add<kuge::Body3D>(crate, kuge::Body3D{});
            match.track(crate, CRATE, 0);
        }
    }

    void collectCoins(kw::World& world)
    {
        std::vector<kuge::TriggerEvent3D> entered;

        for (const auto& event : world.getResource<kuge::Physics3D>().events()) {
            if (event.entered) {
                entered.push_back(event);
            }
        }
        for (const auto& event : entered) {
            // (a coin another player took in this same tick is gone already: the entity is checked each time)
            if (!world.has<Coin>(event.trigger) || !world.has<Owned>(event.other) || !world.has<Score>(event.other)) {
                continue;
            }
            world.get<Score>(event.other).points += 1;
            world.remove(event.trigger);
        }
    }

    Outcome outcome(kw::World& world)
    {
        for ([[maybe_unused]] kw::Entity coin : world.view<Coin>()) {
            return Outcome::Playing;
        }
        return Outcome::Won;
    }

    std::optional<kw::Entity> playerOf(kw::World& world, std::uint32_t player)
    {
        for (kw::Entity entity : world.view<Owned>()) {
            if (world.get<Owned>(entity).player == player && world.has<kuge::Body3D>(entity)) {
                return entity;
            }
        }
        return std::nullopt;
    }

}
