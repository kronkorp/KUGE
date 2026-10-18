#include "Arena.hpp"
#include <algorithm>
#include <limits>

namespace arena
{

    namespace
    {
        constexpr Vec3 UP{0.0f, 1.0f, 0.0f};
        constexpr Vec3 RIGHT{1.0f, 0.0f, 0.0f};

        // Body turned the way the player looks: at yaw 0 it faces -z, and positive turns right
        k3::Math::Vector4f bodyRotation(float yaw)
        {
            return kuge::axisAngle(UP, -yaw);
        }

        // The entities that have C, by increasing number: what the rules do never depends on how the World stores them
        template<typename C>
        std::vector<kw::Entity> sorted(kw::World& world)
        {
            std::vector<kw::Entity> entities;

            for (kw::Entity entity : world.view<C>()) {
                entities.push_back(entity);
            }
            std::sort(entities.begin(), entities.end());
            return entities;
        }


        struct Clip { const char* name; bool loop; };

        // The clip that goes with what a character does: in the air, firing, running, or standing
        Clip clipFor(const Fighter& fighter, const kuge::Body3D& body)
        {
            const float speed2 = body.velocity.x * body.velocity.x + body.velocity.z * body.velocity.z;

            if (fighter.airborne >= 4) {
                return {"jump", false};     // (a few ticks of air, so that a bump on a ramp is not a jump)
            }
            if (fighter.flash > 0) {
                return {"shoot", false};
            }
            return speed2 > 1.0f ? Clip{"run", true} : Clip{"idle", true};
        }
    }

    void buildLevel(kw::World& world)
    {
        world.addResource<kuge::Physics3D>();
        for (const Piece& piece : level()) {
            const kw::Entity entity = world.create();
            kuge::Transform3D transform;
            kuge::Collider3D collider = kuge::Collider3D::box(piece.size.x, piece.size.y, piece.size.z);

            transform.translation = piece.center;
            transform.rotation = piece.rotation;
            collider.friction = 0.6f;
            world.add<kuge::Transform3D>(entity, transform);
            world.add<kuge::Collider3D>(entity, collider);
        }
    }

    kw::Entity buildPlayer(kw::World& world, Vec3 at, float yaw)
    {
        const kw::Entity player = world.create();
        kuge::Transform3D transform;
        kuge::Body3D body;

        transform.translation = at;
        transform.rotation = bodyRotation(yaw);
        body.lockRotation = true;
        body.mass = 70.0f;
        world.add<kuge::Transform3D>(player, transform);
        world.add<kuge::Collider3D>(player, kuge::Collider3D::capsule(PLAYER_RADIUS, PLAYER_HEIGHT));
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
        world.get<kuge::Transform3D>(player).rotation = bodyRotation(yaw);
    }

    Vec3 aim(float yaw, float pitch)
    {
        return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    }

    k3::Math::Vector4f facing(float yaw, float pitch)
    {
        return multiply(bodyRotation(yaw), kuge::axisAngle(RIGHT, pitch));
    }

    void buildGame(kw::World& world)
    {
        Match& match = world.getResource<Match>();

        for (const Vec3& at : crates()) {
            const kw::Entity crate = world.create();
            kuge::Transform3D transform;
            kuge::Collider3D collider = kuge::Collider3D::box(1.0f, 1.0f, 1.0f);
            kuge::Body3D body;

            transform.translation = at;
            collider.friction = 0.6f;
            body.mass = 60.0f;     // heavy, but a player pushes it
            world.add<kuge::Transform3D>(crate, transform);
            world.add<kuge::Collider3D>(crate, collider);
            world.add<kuge::Body3D>(crate, body);
            match.track(crate, CRATE, 0);
        }
    }

    kw::Entity spawnPlayer(kw::World& world, std::uint32_t player)
    {
        Match& match = world.getResource<Match>();
        const auto& places = spawns();
        std::vector<Vec3> others;

        for (kw::Entity entity : sorted<Owned>(world)) {
            if (world.has<kuge::Body3D>(entity)) {
                others.push_back(world.get<kuge::Transform3D>(entity).translation);
            }
        }
        // The place that is farthest from every other player (the first of them if nobody is there yet)
        std::size_t best = player % places.size();
        float farthest = -1.0f;

        if (!others.empty()) {
            for (std::size_t i = 0; i < places.size(); ++i) {
                float nearest = std::numeric_limits<float>::max();

                for (const Vec3& other : others) {
                    nearest = std::min(nearest, Vec3::length(other - places[i].at));
                }
                if (nearest > farthest) {
                    farthest = nearest;
                    best = i;
                }
            }
        }
        const kw::Entity body = buildPlayer(world, places[best].at, places[best].yaw);

        world.add<Health>(body, Health{});
        world.add<Score>(body, Score{match.frags[player]});
        world.add<Owned>(body, Owned{player});
        world.add<Fighter>(body, Fighter{});
        world.add<kuge::AnimationState3D>(body, kuge::AnimationState3D{"idle", true, 0});
        match.track(body, PLAYER, player);
        ++match.spawned;
        return body;
    }

    void fire(kw::World& world, kw::Entity shooter, const Steer& steer)
    {
        Match& match = world.getResource<Match>();
        Fighter& fighter = world.get<Fighter>(shooter);

        if (fighter.cooldown > 0) {
            return;
        }
        fighter.cooldown = FIRE_COOLDOWN;
        fighter.flash = 10;
        // The same clip again starts over: the serial moves
        auto& state = world.get<kuge::AnimationState3D>(shooter);

        state = kuge::AnimationState3D{"shoot", false, static_cast<std::uint8_t>(state.serial + 1)};

        const float yaw = static_cast<float>(steer.yaw) / 1000.0f;
        const float pitch = static_cast<float>(steer.pitch) / 1000.0f;
        const Vec3 direction = aim(yaw, pitch);
        const Vec3 eye = world.get<kuge::Transform3D>(shooter).translation + UP * EYE_HEIGHT;
        const kw::Entity bullet = world.create();
        kuge::Transform3D transform;

        transform.translation = eye + direction * 0.7f;     // (out of the body that fires it)
        transform.rotation = facing(yaw, pitch);
        world.add<kuge::Transform3D>(bullet, transform);
        world.add<Projectile>(bullet, Projectile{direction * BULLET_SPEED, world.get<Owned>(shooter).player, BULLET_LIFE});
        match.track(bullet, BULLET, world.get<Owned>(shooter).player);
    }

    namespace
    {
        // A body that dies: a corpse takes its place, the player comes back later, and the killer scores
        void kill(kw::World& world, kw::Entity victim, std::uint32_t killer)
        {
            Match& match = world.getResource<Match>();
            const std::uint32_t player = world.get<Owned>(victim).player;
            const kw::Entity corpse = world.create();
            kuge::Transform3D transform = world.get<kuge::Transform3D>(victim);

            world.add<kuge::Transform3D>(corpse, transform);
            world.add<Corpse>(corpse, Corpse{});
            world.add<kuge::AnimationState3D>(corpse, kuge::AnimationState3D{"death", false, 0});
            match.track(corpse, CORPSE, player);
            world.remove(victim);
            match.respawns.push_back({player, RESPAWN_DELAY});
            if (killer != player) {
                const int frags = ++match.frags[killer];

                if (const auto body = playerOf(world, killer)) {
                    world.get<Score>(*body).frags = frags;
                }
                if (frags >= FRAG_LIMIT && match.winner == 0) {
                    match.winner = killer;
                }
            }
        }

        // A bullet that hit `entity`: a player is hurt, a crate is pushed, anything stops it. Returns false if the
        // bullet goes on (it was its own shooter's body)
        bool hit(kw::World& world, const Projectile& bullet, kw::Entity entity, Vec3 direction)
        {
            if (world.has<Owned>(entity) && world.has<Health>(entity)) {
                if (world.get<Owned>(entity).player == bullet.owner) {
                    return false;
                }
                if ((world.get<Health>(entity).points -= DAMAGE) <= 0) {
                    kill(world, entity, bullet.owner);
                }
            } else if (world.has<kuge::Body3D>(entity)) {
                auto& velocity = world.get<kuge::Body3D>(entity).velocity;

                velocity = velocity + direction * 3.0f;    // a shove
            }
            return true;
        }
    }

    void stepRules(kw::World& world)
    {
        Match& match = world.getResource<Match>();
        const kuge::Physics3D& physics = world.getResource<kuge::Physics3D>();

        // The gun cools, and what each character does picks its clip
        for (kw::Entity entity : sorted<Fighter>(world)) {
            Fighter& fighter = world.get<Fighter>(entity);
            const kuge::Body3D& body = world.get<kuge::Body3D>(entity);

            fighter.cooldown = std::max(0, fighter.cooldown - 1);
            fighter.flash = std::max(0, fighter.flash - 1);
            fighter.airborne = body.grounded ? 0 : fighter.airborne + 1;
            const Clip clip = clipFor(fighter, body);
            auto& state = world.get<kuge::AnimationState3D>(entity);

            if (state.clip != clip.name) {
                state = kuge::AnimationState3D{clip.name, clip.loop, state.serial};
            }
        }
        // The bullets fly: each tick, a ray from where it was to where it goes (a bullet is too fast to be a body)
        for (kw::Entity entity : sorted<Projectile>(world)) {
            Projectile& bullet = world.get<Projectile>(entity);
            kuge::Transform3D& transform = world.get<kuge::Transform3D>(entity);
            const Vec3 step = bullet.velocity * static_cast<float>(TICK_DT);
            const float length = Vec3::length(step);

            if (const auto found = physics.raycast(transform.translation, step, length)) {
                if (hit(world, bullet, found->entity, step / length)) {
                    world.remove(entity);
                    continue;
                }
            }
            transform.translation = transform.translation + step;
            if (--bullet.life <= 0) {
                world.remove(entity);
            }
        }
        for (kw::Entity entity : sorted<Corpse>(world)) {
            if (--world.get<Corpse>(entity).life <= 0) {
                world.remove(entity);
            }
        }
        // The dead come back
        std::vector<Match::Respawn> waiting;

        for (Match::Respawn& respawn : match.respawns) {
            if (--respawn.ticks <= 0) {
                spawnPlayer(world, respawn.player);
            } else {
                waiting.push_back(respawn);
            }
        }
        match.respawns = std::move(waiting);
    }

    bool finished(kw::World& world)
    {
        return world.getResource<Match>().winner != 0;
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
