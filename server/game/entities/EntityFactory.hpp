#pragma once
#include "PlayerEntity.hpp"
#include "GhostPlayerEntity.hpp"
#include "common/MessageTypes.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>
#include <functional>
#include <unordered_map>

namespace voxelmmo {

using PlayerSpawnFn = std::function<void(entt::registry&, entt::entity,
                                         int32_t, int32_t, int32_t, PlayerId)>;

inline const std::unordered_map<EntityType, PlayerSpawnFn> playerFactories = {
    { EntityType::PLAYER,       PlayerEntity::spawn      },
    { EntityType::GHOST_PLAYER, GhostPlayerEntity::spawn },
};

} // namespace voxelmmo
