#include "PredictedSpawns.hpp"

std::size_t kuge::replication::registerPredictionKey(ReplicationRegistry& registry)
{
    return registry.component<PredictionKey>("PredictionKey", Replicate::OnSpawn,
        [](ByteWriter& out, const PredictionKey& key) { out.write(key.player); out.write(key.sequence); out.write(key.index); },
        [](ByteReader& in) {
            PredictionKey key;

            key.player = in.read<NetworkId>();
            key.sequence = in.read<std::uint32_t>();
            key.index = in.read<std::uint8_t>();
            return key;
        });
}

void kuge::replication::PredictedSpawns::add(kw::Entity predicted, PredictionKey key)
{
    if (m_world.has<PredictionKey>(predicted)) {
        m_world.get<PredictionKey>(predicted) = key;
    } else {
        m_world.add<PredictionKey>(predicted, key);
    }
    m_pending.push_back({predicted, key, 0.0});
}

void kuge::replication::PredictedSpawns::forget(kw::Entity predicted)
{
    std::erase_if(m_pending, [predicted](const Pending& one) { return one.entity == predicted; });
}

void kuge::replication::PredictedSpawns::update(double seconds)
{
    // The real entities that carry a key (only those the server replicated have a Replicated)
    std::vector<std::pair<PredictionKey, kw::Entity>> real;

    for (kw::Entity entity : m_world.view<Replicated, PredictionKey>()) {
        real.emplace_back(m_world.get<PredictionKey>(entity), entity);
    }
    for (std::size_t i = 0; i < m_pending.size();) {
        Pending& one = m_pending[i];
        bool done = false;

        for (const auto& [key, entity] : real) {
            if (key == one.key) {
                if (m_confirmed) {
                    m_confirmed(one.entity, entity);
                } else {
                    m_world.remove(one.entity);
                }
                done = true;
                break;
            }
        }
        if (!done) {
            one.age += seconds;
            if (one.age >= m_config.timeout) {
                if (m_refused) {
                    m_refused(one.entity);
                }
                m_world.remove(one.entity);
                done = true;
            }
        }
        if (done) {
            m_pending.erase(m_pending.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}
