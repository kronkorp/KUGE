#include "VisibilityFilter.hpp"
#include <cmath>

void kuge::replication::VisibilityFilter::newTick(std::uint32_t tick)
{
    m_tick = tick;
    m_viewers.clear();
    m_placed.clear();
    m_answers.clear();
    // Pairs that have not been seen for a while are forgotten (an entity that left, a client that went)
    for (auto it = m_lastSeen.begin(); it != m_lastSeen.end();) {
        it = m_tick > it->second + m_config.graceTicks + 600 ? m_lastSeen.erase(it) : std::next(it);
    }
}

bool kuge::replication::VisibilityFilter::canSee(const VisibilityConfig::Viewer& viewer, const VisibilityConfig::Placed& target)
{
    using Vec3 = VisibilityConfig::Vec3;
    const float ahead = static_cast<float>(m_config.lookAhead);
    // Where eyes and target are now, and where they will be: coming round a corner shows early
    const Vec3 eyes[2] = {viewer.eye, viewer.eye + viewer.velocity * ahead};
    const Vec3 feet[2] = {target.position, target.position + target.velocity * ahead};

    for (const Vec3& eye : eyes) {
        for (const Vec3& at : feet) {
            for (const Vec3& offset : m_config.samples) {
                ++m_stats.rays;
                if (!m_config.clear || m_config.clear(eye, at + offset)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool kuge::replication::VisibilityFilter::operator()(net::ConnectionId connection, NetworkId id, const EntityRecord& record)
{
    const Key key{connection, id};
    const auto answered = m_answers.find(key);

    if (answered != m_answers.end()) {
        return answered->second;   // (the same pair is asked again by a second snapshot of the tick)
    }
    ++m_stats.asked;
    const auto decide = [&]() -> bool {
        if (m_config.always && m_config.always(connection, id, record)) {
            return true;
        }
        auto viewer = m_viewers.find(connection);

        if (viewer == m_viewers.end()) {
            viewer = m_viewers.emplace(connection, m_config.viewer ? m_config.viewer(connection) : std::nullopt).first;
        }
        auto placed = m_placed.find(id);

        if (placed == m_placed.end()) {
            placed = m_placed.emplace(id, m_config.position ? m_config.position(id) : std::nullopt).first;
        }
        if (!placed->second) {
            return true;   // not a thing of the world
        }
        if (!viewer->second) {
            return false;  // a client with no eyes (dead, not spawned): it sees nothing that is not "always"
        }
        const auto apart = placed->second->position - viewer->second->eye;

        if (std::sqrt(apart.x * apart.x + apart.y * apart.y + apart.z * apart.z) > m_config.viewDistance) {
            ++m_stats.tooFar;
            return false;
        }
        if (canSee(*viewer->second, *placed->second)) {
            m_lastSeen[key] = m_tick;
            ++m_stats.seen;
            return true;
        }
        const auto before = m_lastSeen.find(key);

        if (before != m_lastSeen.end() && m_tick - before->second <= m_config.graceTicks) {
            ++m_stats.kept;
            return true;
        }
        ++m_stats.hidden;
        return false;
    };
    const bool result = decide();

    m_answers.emplace(key, result);
    return result;
}
