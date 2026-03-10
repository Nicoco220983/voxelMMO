#pragma once
#include "common/Types.hpp"
#include <set>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Metadata the GameEngine tracks for each connected gateway.
 * 
 * Note: Per-player watched chunks and state tracking has moved to GatewayEngine.
 * GameEngine only needs to track which players are routed through this gateway.
 */
struct GatewayInfo {
    std::set<PlayerId> players;  ///< Players routed through this gateway.
};

} // namespace voxelmmo
