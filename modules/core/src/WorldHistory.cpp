#include "WorldHistory.hpp"
#include <algorithm>
#include <cmath>

void kuge::WorldHistory::record(kw::World& world, std::uint32_t tick, const std::function<bool(kw::World&, kw::Entity)>& select)
{
    Frame frame;
    std::vector<kw::Entity> entities;

    frame.tick = tick;
    for (const Kept& kept : m_kept) {
        kept.collect(world, entities);
    }
    std::sort(entities.begin(), entities.end());
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    for (kw::Entity entity : entities) {
        if (select && !select(world, entity)) {
            continue;
        }
        std::vector<std::any>& values = frame.values[identityOf(world, entity)];

        for (const Kept& kept : m_kept) {
            values.push_back(kept.has(world, entity) ? kept.get(world, entity) : std::any());
        }
    }
    if (!m_frames.empty() && m_frames.back().tick >= tick) {
        m_frames.clear();   // the clock went back (a new match): the old memory is of another time
    }
    m_frames.push_back(std::move(frame));
    while (m_frames.size() > std::max<std::size_t>(m_config.windowTicks, 1)) {
        m_frames.pop_front();
    }
}

bool kuge::WorldHistory::rewound(kw::World& world, double tick, const std::function<void(const Rewound&)>& fn)
{
    if (m_frames.empty() || tick < static_cast<double>(m_frames.front().tick) || tick > static_cast<double>(m_frames.back().tick)) {
        return false;
    }
    // The two recorded ticks around it
    auto after = std::lower_bound(m_frames.begin(), m_frames.end(), tick, [](const Frame& frame, double wanted) { return static_cast<double>(frame.tick) < wanted; });
    const Frame& later = *after;
    const Frame& earlier = after == m_frames.begin() || static_cast<double>(later.tick) == tick ? later : *std::prev(after);
    const float t = later.tick == earlier.tick ? 0.0f : static_cast<float>((tick - earlier.tick) / static_cast<double>(later.tick - earlier.tick));
    // Which current entity is which identity
    std::vector<kw::Entity> entities;

    for (const Kept& kept : m_kept) {
        kept.collect(world, entities);
    }
    std::sort(entities.begin(), entities.end());
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());

    struct Saved
    {
        kw::Entity           entity;
        std::vector<std::any> values;
    };
    std::vector<Saved> saved;
    Rewound view;

    view.m_tick = tick;
    for (kw::Entity entity : entities) {
        const auto then = (t < 0.5f ? earlier : later).values.find(identityOf(world, entity));

        if (then == (t < 0.5f ? earlier : later).values.end()) {
            continue;
        }
        Saved before{entity, {}};

        for (const Kept& kept : m_kept) {
            before.values.push_back(kept.has(world, entity) ? kept.get(world, entity) : std::any());
        }
        saved.push_back(std::move(before));
        view.m_present[entity] = true;
    }
    // Back as they were, whatever happens in fn
    struct Restore
    {
        kw::World&              world;
        const std::vector<Kept>& kept;
        std::vector<Saved>&     saved;

        ~Restore(void)
        {
            for (const Saved& one : saved) {
                for (std::size_t i = 0; i < kept.size(); ++i) {
                    if (one.values[i].has_value()) {
                        kept[i].set(world, one.entity, one.values[i]);
                    }
                }
            }
        }
    } restore{world, m_kept, saved};

    for (const Saved& one : saved) {
        const std::uint64_t who = identityOf(world, one.entity);
        const auto& from = earlier.values.find(who);
        const auto& to = later.values.find(who);

        for (std::size_t i = 0; i < m_kept.size(); ++i) {
            const std::any* a = from != earlier.values.end() && i < from->second.size() && from->second[i].has_value() ? &from->second[i] : nullptr;
            const std::any* b = to != later.values.end() && i < to->second.size() && to->second[i].has_value() ? &to->second[i] : nullptr;

            if (a && b && m_kept[i].blend && t > 0.0f && t < 1.0f) {
                m_kept[i].set(world, one.entity, m_kept[i].blend(*a, *b, t));
            } else if (const std::any* nearest = t < 0.5f ? (a ? a : b) : (b ? b : a)) {
                m_kept[i].set(world, one.entity, *nearest);
            }
        }
    }
    fn(view);
    return true;
}

kuge::WorldHistory::Limit kuge::WorldHistory::limit(double requestedTick, std::uint32_t currentTick, double rttSeconds, std::uint32_t tickRate,
    double interpolationSeconds) const
{
    const double now = static_cast<double>(currentTick);

    if (requestedTick > now + 1.0) {
        return {Limit::Verdict::Future, now};
    }
    if (m_frames.empty() || requestedTick < static_cast<double>(m_frames.front().tick)) {
        return {Limit::Verdict::TooOld, requestedTick};
    }
    // What the player can really be looking at: the interpolation delay and a round trip behind, and a tick of slack
    const double believable = now - (interpolationSeconds + std::max(rttSeconds, 0.0)) * static_cast<double>(tickRate) - 1.0;

    if (requestedTick < believable) {
        return {Limit::Verdict::Clamped, std::max(believable, static_cast<double>(m_frames.front().tick))};
    }
    return {Limit::Verdict::Ok, std::min(requestedTick, now)};
}
