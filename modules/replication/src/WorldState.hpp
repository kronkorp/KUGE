#pragma once

#include "Replication.hpp"
#include <functional>
#include <map>
#include <vector>

namespace kuge::replication
{

    //! One replicated entity as the wire knows it: its kind, its owner, and the bytes of its components
    struct EntityRecord
    {
        EntityType                       type  = 0;
        NetworkId                        owner = 0;
        std::map<std::uint8_t, Bytes>    components;   //!< By number in the registry

        bool operator==(const EntityRecord&) const = default;
    };

    //! Everything that is replicated at a moment: what a snapshot is a picture of
    using WorldState = std::map<NetworkId, EntityRecord>;

    //! Reads every entity that has a Replicated
    WorldState captureState(kw::World& world, const ReplicationRegistry& registry);

    //! What to send so that whoever has `baseline` (null: nothing) can build `current`: one record for each entity
    //! that appeared, changed or went. Each record is complete on its own (see applyOps).
    std::vector<Bytes> diffOps(const ReplicationRegistry& registry, const WorldState* baseline, const WorldState& current);

    //! The state that results from `baseline` and the records of diffOps() (concatenated)
    //! @throw SerializerError if the data does not make sense
    WorldState applyOps(const ReplicationRegistry& registry, const WorldState& baseline, std::span<const std::uint8_t> ops);

}
