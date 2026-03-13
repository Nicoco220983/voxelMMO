#pragma once
#include "common/Types.hpp"
#include <set>
#include <unordered_map>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Metadata the GameEngine tracks for each connected gateway.
 */
struct GatewayInfo {
    std::set<PlayerId> players;  ///< Players routed through this gateway.
};

} // namespace voxelmmo
