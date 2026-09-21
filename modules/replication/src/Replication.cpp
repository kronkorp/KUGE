#include "Replication.hpp"
#include "Wire.hpp"

std::uint32_t kuge::replication::ReplicationRegistry::schema(void) const
{
    std::string all;

    for (const auto& entry : m_entries) {
        all += entry.name + ":" + std::to_string(static_cast<int>(entry.mode)) + ";";
    }
    return net::hashName(all);
}
