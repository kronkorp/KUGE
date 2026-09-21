#include "RType.hpp"
#include <cmath>

namespace rtype
{

    namespace
    {
        struct Box
        {
            float left, top, right, bottom;
        };

        Box boxOf(const kuge::Transform2D& transform, kuge::Vec2 size)
        {
            return {transform.position.x - size.x / 2, transform.position.y - size.y / 2,
                transform.position.x + size.x / 2, transform.position.y + size.y / 2};
        }

        bool overlap(const Box& a, const Box& b)
        {
            return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
        }

        template<typename C>
        std::vector<kw::Entity> sorted(kw::World& world)
        {
            std::vector<kw::Entity> entities;
            auto view = world.view<C, kuge::Transform2D>();

            for (kw::Entity e : view) {
                entities.push_back(e);
            }
            std::sort(entities.begin(), entities.end());
            return entities;
        }
    }

    std::vector<kw::Entity> ships(kw::World& world)
    {
        return sorted<Gun>(world);
    }

    void fire(kw::World& world, kw::Entity ship)
    {
        auto& gun = world.get<Gun>(ship);
        auto& match = world.getResource<Match>();

        if (gun.cooldown > 0) {
            return;
        }
        gun.cooldown = settings().fireCooldown;
        const kw::Entity bullet = world.create();
        const auto& from = world.get<kuge::Transform2D>(ship).position;

        world.add<kuge::Transform2D>(bullet, kuge::Transform2D{{from.x + SHIP_SIZE.x / 2, from.y}});
        world.add<Bullet>(bullet, Bullet{{BULLET_SPEED, 0.0f}});
        world.add<Owned>(bullet, world.get<Owned>(ship));
        if (match.track) {
            match.track(bullet, BULLET, world.get<Owned>(ship).player);
        }
    }

    void stepRules(kw::World& world, double dt)
    {
        auto& match = world.getResource<Match>();
        const float step = static_cast<float>(dt);

        ++match.tick;
        for (kw::Entity ship : ships(world)) {
            auto& gun = world.get<Gun>(ship);

            gun.cooldown -= gun.cooldown > 0 ? 1 : 0;
        }
        // Bullets fly, and leave the arena
        for (kw::Entity bullet : sorted<Bullet>(world)) {
            auto& position = world.get<kuge::Transform2D>(bullet).position;

            position.x += world.get<Bullet>(bullet).velocity.x * step;
            position.y += world.get<Bullet>(bullet).velocity.y * step;
            if (position.x > ARENA_W + 20.0f) {
                world.remove(bullet);
            }
        }
        // Enemies come, from the right, one every `spawnEvery` ticks
        if (match.spawned < settings().waveSize && match.tick % settings().spawnEvery == 0) {
            const kw::Entity enemy = world.create();
            const float y = 30.0f + static_cast<float>(match.rng() % static_cast<unsigned>(ARENA_H - 60.0f));

            world.add<kuge::Transform2D>(enemy, kuge::Transform2D{{ARENA_W + 10.0f, y}});
            world.add<Enemy>(enemy, Enemy{y, static_cast<float>(match.rng() % 628) / 100.0f});
            world.add<Health>(enemy, Health{settings().enemyHealth});
            ++match.spawned;
            if (match.track) {
                match.track(enemy, ENEMY, 0);
            }
        }
        // ...and cross the screen, waving
        for (kw::Entity enemy : sorted<Enemy>(world)) {
            auto& position = world.get<kuge::Transform2D>(enemy).position;
            const auto& data = world.get<Enemy>(enemy);

            position.x -= settings().enemySpeed * step;
            position.y = data.baseY + data.amplitude * std::sin(data.phase + static_cast<float>(match.tick) * 0.05f);
            if (position.x < -20.0f) {
                world.remove(enemy);
            }
        }
        // Bullets hit enemies
        const auto enemies = sorted<Enemy>(world);

        for (kw::Entity bullet : sorted<Bullet>(world)) {
            const Box shot = boxOf(world.get<kuge::Transform2D>(bullet), BULLET_SIZE);

            for (kw::Entity enemy : enemies) {
                if (!world.has<Enemy>(enemy) || !overlap(shot, boxOf(world.get<kuge::Transform2D>(enemy), ENEMY_SIZE))) {
                    continue;
                }
                const std::uint32_t owner = world.get<Owned>(bullet).player;

                world.remove(bullet);
                if (--world.get<Health>(enemy).points <= 0) {
                    world.remove(enemy);
                    ++match.killed;
                    for (kw::Entity ship : ships(world)) {
                        if (world.get<Owned>(ship).player == owner) {
                            ++world.get<Score>(ship).points;
                        }
                    }
                }
                break;
            }
        }
        // Enemies hit ships
        for (kw::Entity enemy : sorted<Enemy>(world)) {
            const Box body = boxOf(world.get<kuge::Transform2D>(enemy), ENEMY_SIZE);

            for (kw::Entity ship : ships(world)) {
                if (overlap(body, boxOf(world.get<kuge::Transform2D>(ship), SHIP_SIZE))) {
                    --world.get<Health>(ship).points;
                    world.remove(enemy);
                    break;
                }
            }
        }
        for (kw::Entity ship : ships(world)) {
            if (world.get<Health>(ship).points <= 0) {
                world.remove(ship);
            }
        }
    }

    Outcome outcome(kw::World& world, bool anyShipEver)
    {
        const auto& match = world.getResource<Match>();

        if (anyShipEver && ships(world).empty()) {
            return Outcome::Lost;
        }
        if (match.spawned >= settings().waveSize && sorted<Enemy>(world).empty()) {
            return Outcome::Won;
        }
        return Outcome::Playing;
    }

}
