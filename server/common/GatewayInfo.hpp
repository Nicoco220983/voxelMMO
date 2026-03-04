#pragma once
#include "Types.hpp"
#include <set>
#include <unordered_map>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Metadata the GameEngine tracks for each connected gateway.
 */
struct GatewayInfo {
    std::set<PlayerId>                    players;        ///< Players routed through this gateway.
    std::set<ChunkId>                     watchedChunks;  ///< Chunks the gateway currently needs state for.
    std::unordered_map<ChunkId, uint32_t> lastStateTick; ///< Tick of the last state (snapshot or delta) sent per chunk.
};

} // namespace voxelmmo
