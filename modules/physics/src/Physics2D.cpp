#include "Physics2D.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    using namespace kuge;

    // A body resting on a wall may overlap it by the rounding error of a float.
    // Along the other axis, walls are only met beyond this depth.
    constexpr float SKIN = 0.01f;

    // A body touches a wall if it is closer than this to it
    constexpr float PROBE = 0.05f;

    float axisOf(Vec2 v, int axis) { return axis == 0 ? v.x : v.y; }

    // The box, thinner by the skin on both sides of the axis across
    Aabb thinnerAcross(Aabb box, int axis)
    {
        if (axis == 0) {
            box.min.y += SKIN;
            box.max.y -= SKIN;
        } else {
            box.min.x += SKIN;
            box.max.x -= SKIN;
        }
        return box;
    }

    // How far to move box along an axis, to the sign side, to leave wall
    float shiftOutOf(const Aabb& box, const Aabb& wall, int axis, float sign)
    {
        return sign > 0.0f ? axisOf(wall.min, axis) - axisOf(box.max, axis)
                           : axisOf(wall.max, axis) - axisOf(box.min, axis);
    }

    // The first solid tile met by a ray that starts inside the level
    std::optional<RayHit> walkTiles(const SolidGrid& grid, Vec2 origin, Vec2 direction, float maxDistance)
    {
        int x, y;
        const int stepX = direction.x > 0.0f ? 1 : -1;
        const int stepY = direction.y > 0.0f ? 1 : -1;
        const float infinity = std::numeric_limits<float>::infinity();

        grid.tileAt(origin, x, y);
        if (grid.isSolid(x, y)) {
            return RayHit{0.0f, {}};
        }
        // Distance along the ray to the next vertical / horizontal line of the grid
        float nextX = infinity;
        float nextY = infinity;
        const float deltaX = direction.x != 0.0f ? grid.tileSize / std::fabs(direction.x) : infinity;
        const float deltaY = direction.y != 0.0f ? grid.tileSize / std::fabs(direction.y) : infinity;

        if (direction.x != 0.0f) {
            nextX = (grid.origin.x + grid.tileSize * static_cast<float>(stepX > 0 ? x + 1 : x) - origin.x) / direction.x;
        }
        if (direction.y != 0.0f) {
            nextY = (grid.origin.y + grid.tileSize * static_cast<float>(stepY > 0 ? y + 1 : y) - origin.y) / direction.y;
        }
        for (int steps = 0; steps < grid.width + grid.height + 4; ++steps) {
            Vec2 normal;
            float distance;

            if (nextX < nextY) {
                distance = nextX;
                nextX += deltaX;
                x += stepX;
                normal = {static_cast<float>(-stepX), 0.0f};
            } else {
                distance = nextY;
                nextY += deltaY;
                y += stepY;
                normal = {0.0f, static_cast<float>(-stepY)};
            }
            if (distance > maxDistance) {
                return std::nullopt;
            }
            if (grid.isSolid(x, y)) {
                return RayHit{distance, normal};
            }
            if (x < -1 || y < -1 || x > grid.width || y > grid.height) {
                return std::nullopt;   // left the level for good
            }
        }
        return std::nullopt;
    }

    std::optional<RayHit> rayTiles(const SolidGrid& grid, Vec2 origin, Vec2 direction, float maxDistance)
    {
        if (grid.empty()) {
            return std::nullopt;
        }
        const Aabb level{grid.origin, {grid.origin.x + grid.tileSize * static_cast<float>(grid.width),
            grid.origin.y + grid.tileSize * static_cast<float>(grid.height)}};

        if (origin.x >= level.min.x && origin.x < level.max.x && origin.y >= level.min.y && origin.y < level.max.y) {
            return walkTiles(grid, origin, direction, maxDistance);
        }
        // From outside: start where the ray enters the level, a hair inside
        const auto entry = rayAabb(origin, direction, maxDistance, level);

        if (!entry) {
            return std::nullopt;
        }
        const float before = entry->distance + 1e-3f;
        const auto hit = walkTiles(grid, origin + direction * before, direction, maxDistance - before);

        if (!hit) {
            return std::nullopt;
        }
        return RayHit{hit->distance + before, hit->normal};
    }
}

// A body being moved: its component, and its box relative to its position
struct kuge::Physics2D::Mover
{
    Transform2D*  transform;
    Body*         body;
    Collider      collider;
    Vec2          half;
    Vec2          offset;

    Aabb boxAt(Vec2 position) const { return Aabb::fromCenter(position + offset, half); }
};

kuge::Physics2D::Physics2D(PhysicsConfig configuration) : config(configuration)
{
}

bool kuge::Physics2D::touches(const Item& a, const Item& b)
{
    const bool circleA = a.collider.shape == Collider::Shape::Circle;
    const bool circleB = b.collider.shape == Collider::Shape::Circle;

    if (circleA && circleB) {
        return overlaps(a.circle, b.circle);
    }
    if (circleA) {
        return overlaps(b.box, a.circle);
    }
    if (circleB) {
        return overlaps(a.box, b.circle);
    }
    return a.box.overlaps(b.box);
}

void kuge::Physics2D::step(kw::World& world, float dt)
{
    m_solids.setCellSize(config.cellSize);
    m_all.setCellSize(config.cellSize);
    m_events.clear();
    collect(world);
    moveBodies(world, dt);
    findTriggers(world);
}

// Everything that has a Collider, by entity: the ECS does not promise any order
void kuge::Physics2D::collect(kw::World& world)
{
    m_entities.clear();
    auto view = world.view<Collider, Transform2D>();
    for (kw::Entity entity : view) {
        m_entities.push_back(entity);
    }
    std::sort(m_entities.begin(), m_entities.end());

    m_items.clear();
    m_items.reserve(m_entities.size());
    for (kw::Entity entity : m_entities) {
        const Transform2D& transform = world.get<Transform2D>(entity);
        Item item;
        const Vec2 scale{std::fabs(transform.scale.x), std::fabs(transform.scale.y)};

        item.entity = entity;
        item.collider = world.get<Collider>(entity);
        const Vec2 center = transform.position + Vec2{item.collider.offset.x * scale.x, item.collider.offset.y * scale.y};

        if (item.collider.shape == Collider::Shape::Circle) {
            const float radius = item.collider.size.x * std::max(scale.x, scale.y);

            item.circle = {center, radius};
            item.box = Aabb::fromCenter(center, {radius, radius});
        } else {
            item.box = Aabb::fromCenter(center, {item.collider.size.x * scale.x * 0.5f, item.collider.size.y * scale.y * 0.5f});
        }
        item.mover = world.has<Body>(entity) && world.get<Body>(entity).type != Body::Type::Static;
        m_items.push_back(item);
    }
}

void kuge::Physics2D::moveBodies(kw::World& world, float dt)
{
    // The walls: what does not move, and stops
    m_solids.clear();
    for (std::uint32_t i = 0; i < m_items.size(); ++i) {
        if (!m_items[i].mover && !m_items[i].collider.trigger) {
            m_solids.insert(i, m_items[i].box);
        }
    }
    m_solids.finish();

    for (std::uint32_t i = 0; i < m_items.size(); ++i) {
        if (!m_items[i].mover) {
            continue;
        }
        Item& item = m_items[i];
        Mover mover;

        mover.transform = &world.get<Transform2D>(item.entity);
        mover.body = &world.get<Body>(item.entity);
        mover.collider = item.collider;
        mover.half = item.box.half();
        mover.offset = item.box.center() - mover.transform->position;

        Body& body = *mover.body;

        if (body.type == Body::Type::Dynamic) {
            body.velocity += config.gravity * (body.gravityScale * dt);
            body.velocity.y = std::min(body.velocity.y, config.maxFallSpeed);
        }
        body.contacts = {};
        moveAlong(mover, 0, body.velocity.x * dt);
        moveAlong(mover, 1, body.velocity.y * dt);
        findContacts(mover);

        // Later steps (triggers, questions) see it where it ended
        item.box = mover.boxAt(mover.transform->position);
        item.circle.center = item.box.center();
    }
}

// Moves along one axis and gets out of the walls that stop it. It goes by
// steps smaller than the body, so that a fast one cannot jump over a thin wall.
void kuge::Physics2D::moveAlong(Mover& mover, int axis, float delta)
{
    if (delta == 0.0f) {
        return;
    }
    const float sign = delta > 0.0f ? 1.0f : -1.0f;
    const float reach = std::max(axisOf(mover.half, axis), 1.0f);
    const int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(delta) / reach)));
    float& position = axis == 0 ? mover.transform->position.x : mover.transform->position.y;

    for (int step = 0; step < steps; ++step) {
        position += delta / static_cast<float>(steps);
        bool blocked = false;

        // Walls that are colliders
        Aabb box = thinnerAcross(mover.boxAt(mover.transform->position), axis);
        m_solids.query(box, m_candidates);
        for (std::uint32_t index : m_candidates) {
            const Item& wall = m_items[index];
            std::optional<float> shift;

            if (!compatible(mover.collider, wall.collider)) {
                continue;
            }
            if (wall.collider.shape == Collider::Shape::Circle) {
                shift = pushOutAlongAxis(box, wall.circle, axis, sign);
            } else if (box.overlaps(wall.box)) {
                shift = shiftOutOf(box, wall.box, axis, sign);
            }
            if (shift) {
                position += *shift;
                box = thinnerAcross(mover.boxAt(mover.transform->position), axis);
                blocked = true;
            }
        }
        // Solid tiles, row by row
        if (!tiles.empty()) {
            const float size = tiles.tileSize;
            const int x0 = static_cast<int>(std::floor((box.min.x - tiles.origin.x) / size));
            const int x1 = static_cast<int>(std::ceil((box.max.x - tiles.origin.x) / size)) - 1;
            const int y0 = static_cast<int>(std::floor((box.min.y - tiles.origin.y) / size));
            const int y1 = static_cast<int>(std::ceil((box.max.y - tiles.origin.y) / size)) - 1;

            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    if (tiles.isSolid(x, y) && box.overlaps(tiles.tile(x, y))) {
                        position += shiftOutOf(box, tiles.tile(x, y), axis, sign);
                        box = thinnerAcross(mover.boxAt(mover.transform->position), axis);
                        blocked = true;
                    }
                }
            }
        }
        if (blocked) {
            Body& body = *mover.body;

            (axis == 0 ? body.velocity.x : body.velocity.y) = 0.0f;
            if (axis == 0) {
                (sign > 0.0f ? body.contacts.right : body.contacts.left) = true;
            } else {
                (sign > 0.0f ? body.contacts.down : body.contacts.up) = true;
            }
            return;   // the rest of the movement is into the wall
        }
    }
}

// Is there a wall (a collider or a solid tile) in this box?
bool kuge::Physics2D::touchesWall(const Mover& mover, const Aabb& box) const
{
    std::vector<std::uint32_t> candidates;

    m_solids.query(box, candidates);
    for (std::uint32_t index : candidates) {
        const Item& wall = m_items[index];

        if (!compatible(mover.collider, wall.collider)) {
            continue;
        }
        const bool hit = wall.collider.shape == Collider::Shape::Circle ? overlaps(box, wall.circle) : box.overlaps(wall.box);

        if (hit) {
            return true;
        }
    }
    if (tiles.empty()) {
        return false;
    }
    const float size = tiles.tileSize;
    const int x0 = static_cast<int>(std::floor((box.min.x - tiles.origin.x) / size));
    const int x1 = static_cast<int>(std::ceil((box.max.x - tiles.origin.x) / size)) - 1;
    const int y0 = static_cast<int>(std::floor((box.min.y - tiles.origin.y) / size));
    const int y1 = static_cast<int>(std::ceil((box.max.y - tiles.origin.y) / size)) - 1;

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (tiles.isSolid(x, y) && box.overlaps(tiles.tile(x, y))) {
                return true;
            }
        }
    }
    return false;
}

// Which sides touch a wall, moving or not: a body standing on the floor, or
// pressed against a wall, is in contact even when it has no speed
void kuge::Physics2D::findContacts(Mover& mover) const
{
    const Aabb box = mover.boxAt(mover.transform->position);
    Contacts& contacts = mover.body->contacts;
    Aabb probe = thinnerAcross(box, 0);

    probe.max.x += PROBE;
    contacts.right = contacts.right || touchesWall(mover, probe);
    probe = thinnerAcross(box, 0);
    probe.min.x -= PROBE;
    contacts.left = contacts.left || touchesWall(mover, probe);
    probe = thinnerAcross(box, 1);
    probe.max.y += PROBE;
    contacts.down = contacts.down || touchesWall(mover, probe);
    probe = thinnerAcross(box, 1);
    probe.min.y -= PROBE;
    contacts.up = contacts.up || touchesWall(mover, probe);
}

void kuge::Physics2D::findTriggers(kw::World& world)
{
    m_all.clear();
    for (std::uint32_t i = 0; i < m_items.size(); ++i) {
        m_all.insert(i, m_items[i].box);
    }
    m_all.finish();

    m_previousPairs.swap(m_pairs);
    m_pairs.clear();
    for (std::uint32_t i = 0; i < m_items.size(); ++i) {
        const Item& trigger = m_items[i];

        if (!trigger.collider.trigger) {
            continue;
        }
        m_all.query(trigger.box, m_candidates);
        for (std::uint32_t j : m_candidates) {
            const Item& other = m_items[j];

            if (j == i || !compatible(trigger.collider, other.collider)) {
                continue;
            }
            if (other.collider.trigger && other.entity < trigger.entity) {
                continue;   // two triggers: the pair is reported once
            }
            if (touches(trigger, other)) {
                m_pairs.push_back({trigger.entity, other.entity});
            }
        }
    }
    // Both lists are sorted: what is only in one of them entered or left
    auto now = m_pairs.begin();
    auto before = m_previousPairs.begin();
    auto alive = [&world](kw::Entity entity) { return world.has<Collider>(entity); };

    while (now != m_pairs.end() || before != m_previousPairs.end()) {
        if (before == m_previousPairs.end() || (now != m_pairs.end() && *now < *before)) {
            m_events.push_back({now->trigger, now->other, true});
            ++now;
        } else if (now == m_pairs.end() || *before < *now) {
            // An entity that was destroyed does not "leave": it is gone
            if (alive(before->trigger) && alive(before->other)) {
                m_events.push_back({before->trigger, before->other, false});
            }
            ++before;
        } else {
            ++now;
            ++before;
        }
    }
}

std::optional<kuge::RaycastHit> kuge::Physics2D::raycast(
    Vec2 origin, Vec2 direction, float maxDistance, std::uint32_t mask, bool triggers) const
{
    const float length = direction.length();
    std::optional<RaycastHit> best;
    std::vector<std::uint32_t> candidates;

    if (length == 0.0f || !(maxDistance > 0.0f)) {
        return std::nullopt;
    }
    const Vec2 unit = direction / length;
    const Vec2 end = origin + unit * maxDistance;

    m_all.query({{std::min(origin.x, end.x), std::min(origin.y, end.y)},
                 {std::max(origin.x, end.x), std::max(origin.y, end.y)}}, candidates);
    for (std::uint32_t index : candidates) {
        const Item& item = m_items[index];
        std::optional<RayHit> hit;

        if ((item.collider.layer & mask) == 0 || (item.collider.trigger && !triggers)) {
            continue;
        }
        hit = item.collider.shape == Collider::Shape::Circle
            ? rayCircle(origin, unit, maxDistance, item.circle)
            : rayAabb(origin, unit, maxDistance, item.box);
        if (hit && (!best || hit->distance < best->distance)) {
            best = RaycastHit{item.entity, false, origin + unit * hit->distance, hit->normal, hit->distance};
        }
    }
    // A tile and an entity at the same distance: the entity
    const auto tile = rayTiles(tiles, origin, unit, maxDistance);

    if (tile && (!best || tile->distance < best->distance)) {
        best = RaycastHit{{}, true, origin + unit * tile->distance, tile->normal, tile->distance};
    }
    return best;
}

std::vector<kw::Entity> kuge::Physics2D::overlapRect(const Rect& area, std::uint32_t mask, bool triggers) const
{
    const Aabb box{{area.x, area.y}, {area.right(), area.bottom()}};
    std::vector<std::uint32_t> candidates;
    std::vector<kw::Entity> found;

    m_all.query(box, candidates);
    for (std::uint32_t index : candidates) {
        const Item& item = m_items[index];

        if ((item.collider.layer & mask) == 0 || (item.collider.trigger && !triggers)) {
            continue;
        }
        const bool hit = item.collider.shape == Collider::Shape::Circle ? overlaps(box, item.circle) : item.box.overlaps(box);

        if (hit) {
            found.push_back(item.entity);
        }
    }
    return found;
}

std::vector<kw::Entity> kuge::Physics2D::overlapCircle(Vec2 center, float radius, std::uint32_t mask, bool triggers) const
{
    const Circle circle{center, radius};
    std::vector<std::uint32_t> candidates;
    std::vector<kw::Entity> found;

    m_all.query(Aabb::fromCenter(center, {radius, radius}), candidates);
    for (std::uint32_t index : candidates) {
        const Item& item = m_items[index];

        if ((item.collider.layer & mask) == 0 || (item.collider.trigger && !triggers)) {
            continue;
        }
        const bool hit = item.collider.shape == Collider::Shape::Circle ? overlaps(item.circle, circle) : overlaps(item.box, circle);

        if (hit) {
            found.push_back(item.entity);
        }
    }
    return found;
}
