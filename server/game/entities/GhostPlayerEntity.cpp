#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/EntityFactory.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/InputComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"

namespace voxelmmo::GhostPlayerEntity {

namespace {
    constexpr int32_t PLAYER_BBOX_HX = 128;  // 0.5 voxels
    constexpr int32_t PLAYER_BBOX_HY = 256;  // 1 voxel  
    constexpr int32_t PLAYER_BBOX_HZ = 128;  // 0.5 voxels
}

entt::entity spawnImpl(entt::registry& reg,
                       GlobalEntityId globalId,
                       const EntitySpawnRequest& req)
{
    // Base components (GlobalEntityId, Dirty, ChunkMembership, PendingCreate)
    const entt::entity ent = BaseEntity::spawn(reg, globalId, req.x, req.y, req.z);

    // Ghost-specific components
    reg.emplace<DynamicPositionComponent>(ent, req.x, req.y, req.z, 0, 0, 0, /*grounded=*/true);
    reg.emplace<EntityTypeComponent>(ent, EntityType::GHOST_PLAYER);
    reg.emplace<InputComponent>(ent);
    reg.emplace<PlayerComponent>(ent, req.playerId);
    reg.emplace<BoundingBoxComponent>(ent, PLAYER_BBOX_HX, PLAYER_BBOX_HY, PLAYER_BBOX_HZ);
    reg.emplace<PhysicsModeComponent>(ent, PhysicsMode::GHOST);

    return ent;
}

} // namespace voxelmmo::GhostPlayerEntity
