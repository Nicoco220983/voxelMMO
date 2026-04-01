#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/EntityFactory.hpp"

namespace voxelmmo::GhostPlayerEntity {

entt::entity spawnImpl(entt::registry& reg,
                       GlobalEntityId globalId,
                       const EntitySpawnRequest& req)
{
    return spawn(reg, globalId, req.x, req.y, req.z, req.playerId);
}

} // namespace voxelmmo::GhostPlayerEntity
