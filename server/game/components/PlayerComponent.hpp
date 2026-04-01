#pragma once
#include "common/Types.hpp"
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Emplaced only on player entities; identifies the owning player.
 * 
 * PlayerId is derived deterministically from the session token (first 8 bytes),
 * so reconnection is handled automatically without explicit session storage.
 */
struct PlayerComponent { 
    PlayerId playerId;
};

} // namespace voxelmmo
