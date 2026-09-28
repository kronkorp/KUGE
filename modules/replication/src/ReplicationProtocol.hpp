#pragma once

#include "Wire.hpp"
#include <cstdint>
#include <vector>

namespace kuge::replication
{

    //! One packet of a snapshot: the state of the replicated world at `tick`, as the changes since the one
    //! the client acknowledged (`baseTick`; 0: from nothing). Big snapshots come in several parts.
    struct SnapshotPacket
    {
        KUGE_MESSAGE(SnapshotPacket, schema, tick, baseTick, inputAck, part, parts, ops)
        std::uint32_t              schema = 0;
        std::uint32_t              tick = 0;
        std::uint32_t              baseTick = 0;
        std::uint32_t              inputAck = 0;    //!< The last input of this client that the server has applied
        std::uint16_t              part = 0;
        std::uint16_t              parts = 1;
        std::vector<std::uint8_t>  ops;
    };

    //! The client has the snapshot of this tick (0: it has none, send everything)
    struct SnapshotAck
    {
        KUGE_MESSAGE(SnapshotAck, tick)
        std::uint32_t tick = 0;
    };

}
