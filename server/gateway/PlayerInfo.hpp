#pragma once
#include "common/Types.hpp"
#include <unordered_map>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Metadata the GatewayEngine tracks for each connected player.
 */
struct PlayerInfo {
    /** @brief Latest state tick the player holds for each chunk (0 = none). */
    std::unordered_map<ChunkId, uint32_t> lastStateTick;
};

} // namespace voxelmmo
