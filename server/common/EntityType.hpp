#pragma once
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Registered entity types.
 * 
 * This enum defines all entity types in the game. The values are used in
 * network protocol messages and must stay in sync with client/src/types.js
 */
enum class EntityType : uint8_t {
    PLAYER       = 0,  ///< Full-physics player (gravity + collision)
    GHOST_PLAYER = 1,  ///< Ghost player (noclip, no gravity)
    SHEEP        = 2,  ///< Passive mob: wanders randomly, blocked by voxels
    GOBLIN       = 3,  ///< Hostile mob: wanders, chases and attacks players
};

} // namespace voxelmmo
