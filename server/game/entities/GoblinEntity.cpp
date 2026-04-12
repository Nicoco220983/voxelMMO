#include "game/entities/GoblinEntity.hpp"
#include "game/entities/EntityFactory.hpp"
#include "game/components/HealthComponent.hpp"

namespace voxelmmo::GoblinEntity {

entt::entity spawn(entt::registry& reg,
                   GlobalEntityId globalId,
                   SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z,
                   uint32_t startTick)
{
    // Base components (GlobalEntityId, Dirty, ChunkMembership, PendingCreate)
    const entt::entity ent = BaseEntity::spawn(reg, globalId, x, y, z);

    // Goblin-specific components
    reg.emplace<DynamicPositionComponent>(ent, x, y, z, 0, 0, 0, /*grounded=*/true);
    reg.emplace<EntityTypeComponent>(ent, EntityType::GOBLIN);
    reg.emplace<BoundingBoxComponent>(ent, GOBLIN_BBOX_HX, GOBLIN_BBOX_HY, GOBLIN_BBOX_HZ);
    reg.emplace<PhysicsModeComponent>(ent, PhysicsMode::FULL);

    // Start in IDLE state for 2-5 seconds (120-300 ticks at 60tps)
    uint32_t idleTicks = 120 + (startTick % 180);  // 120-299 ticks
    reg.emplace<GoblinBehaviorComponent>(ent,
        GoblinBehaviorComponent::State::IDLE,
        startTick + idleTicks,
        x, z, 0.0f,
        /*targetEntityId=*/0,
        /*aggroCooldownTick=*/0,
        /*attackCooldownTick=*/0);

    // Add health component
    reg.emplace<HealthComponent>(ent, DEFAULT_HEALTH, DEFAULT_HEALTH, /*lastDamageTick=*/0);

    return ent;
}

entt::entity spawnImpl(entt::registry& reg,
                       GlobalEntityId globalId,
                       const EntitySpawnRequest& req)
{
    return spawn(reg, globalId, req.x, req.y, req.z, req.startTick);
}

size_t serializeCreate(entt::registry& reg, entt::entity ent, SafeBufWriter& w) {
    const size_t startOffset = w.offset();

    // For CREATE: each component decides if it needs serialization based on non-default values
    uint8_t flags = 0;
    const auto& pos = reg.get<DynamicPositionComponent>(ent);
    if (pos.isNonDefault()) flags |= POSITION_BIT;

    const auto& behavior = reg.get<GoblinBehaviorComponent>(ent);
    if (behavior.isNonDefault()) flags |= AI_BEHAVIOR_BIT;

    const auto& health = reg.get<HealthComponent>(ent);
    if (health.isNonDefault()) flags |= HEALTH_BIT;

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::GOBLIN));
    w.write(flags);

    // Serialize only non-default components
    if (flags & POSITION_BIT) {
        DynamicPositionComponent::serialize(reg, ent, POSITION_BIT, w);
    }
    if (flags & AI_BEHAVIOR_BIT) {
        behavior.serializeFields(w);
    }
    if (flags & HEALTH_BIT) {
        HealthComponent::serialize(reg, ent, HEALTH_BIT, w);
    }

    return w.offset() - startOffset;
}

size_t serializeUpdate(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w) {
    // Goblin tracks position, behavior, and health
    const uint8_t flags = dirty.dirtyFlags & (POSITION_BIT | AI_BEHAVIOR_BIT | HEALTH_BIT);

    // Nothing to serialize if no tracked components are dirty
    if (flags == 0) {
        return 0;
    }

    const size_t startOffset = w.offset();

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::GOBLIN));
    w.write(flags);
    DynamicPositionComponent::serialize(reg, ent, flags, w);

    // Serialize goblin behavior component if dirty
    if (flags & AI_BEHAVIOR_BIT) {
        const auto& behavior = reg.get<GoblinBehaviorComponent>(ent);
        behavior.serializeFields(w);
    }

    // Serialize health component if dirty
    if (flags & HEALTH_BIT) {
        HealthComponent::serialize(reg, ent, HEALTH_BIT, w);
    }

    return w.offset() - startOffset;
}

} // namespace voxelmmo::GoblinEntity
