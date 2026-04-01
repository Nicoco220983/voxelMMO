#pragma once
#include "common/Types.hpp"
#include <array>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Emplaced only on player entities; replaces the old PlayerEntity::playerId field.
 * 
 * Session token is a 16-byte UUID that persists across reconnects to identify
 * returning players and recover their entity.
 */
struct PlayerComponent { 
    PlayerId playerId;
    /** 16-byte session token for entity recovery. Zeroed if not set. */
    std::array<uint8_t, 16> sessionToken{};
};

} // namespace voxelmmo
