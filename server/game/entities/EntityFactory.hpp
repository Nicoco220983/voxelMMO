#pragma once
#include "PlayerEntity.hpp"
#include "GhostPlayerEntity.hpp"
#include "common/MessageTypes.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>
#include <functional>
#include <unordered_map>

namespace voxelmmo {

/**
 * @brief Player spawn function signature.
 *
 * All player entity spawn methods follow this signature:
 * - reg:       Entity registry
 * - globalId:  Pre-acquired global entity ID
 * - x,y,z:     Spawn position in sub-voxels (chunk computed from position)
 * - playerId:  Persistent player ID
 */
using PlayerSpawnFn = std::function<entt::entity(entt::registry&,
                                                  GlobalEntityId,
                                                  int32_t, int32_t, int32_t,
                                                  PlayerId)>;

/**
 * @brief Map from EntityType to player spawn function.
 *
 * Used by GameEngine::addPlayer() to delegate entity creation to the
 * appropriate spawn method. All methods in this map call BaseEntity::spawn()
 * first to ensure common components are properly initialized.
 */
inline const std::unordered_map<EntityType, PlayerSpawnFn> playerFactories = {
    { EntityType::PLAYER,       PlayerEntity::spawn      },
    { EntityType::GHOST_PLAYER, GhostPlayerEntity::spawn },
};

} // namespace voxelmmo
