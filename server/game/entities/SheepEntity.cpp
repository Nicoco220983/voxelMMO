#include "game/entities/SheepEntity.hpp"
#include "game/entities/EntityFactory.hpp"

namespace voxelmmo::SheepEntity {

entt::entity spawnImpl(entt::registry& reg,
                       GlobalEntityId globalId,
                       const EntitySpawnRequest& req)
{
    // Base components (GlobalEntityId, Dirty, ChunkMembership, PendingCreate)
    const entt::entity ent = BaseEntity::spawn(reg, globalId, req.x, req.y, req.z);

    // Sheep-specific components
    reg.emplace<DynamicPositionComponent>(ent, req.x, req.y, req.z, 0, 0, 0, /*grounded=*/true);
    reg.emplace<EntityTypeComponent>(ent, EntityType::SHEEP);
    reg.emplace<BoundingBoxComponent>(ent, SHEEP_BBOX_HX, SHEEP_BBOX_HY, SHEEP_BBOX_HZ);
    reg.emplace<PhysicsModeComponent>(ent, PhysicsMode::FULL);

    // Start in IDLE state for 2-5 seconds (120-300 ticks at 60tps)
    uint32_t idleTicks = 120 + (req.startTick % 180);
    reg.emplace<SheepBehaviorComponent>(ent,
        SheepBehaviorComponent::State::IDLE,
        req.startTick + idleTicks,
        req.x, req.z, 0.0f);

    return ent;
}

} // namespace voxelmmo::SheepEntity
