#include "game/entities/PlayerEntity.hpp"
#include "game/entities/EntityFactory.hpp"
#include "game/components/JumpComponent.hpp"
#include "game/components/WalkComponent.hpp"
#include "game/components/HealthComponent.hpp"

namespace voxelmmo::PlayerEntity {

entt::entity spawn(entt::registry& reg,
                   GlobalEntityId globalId,
                   SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z,
                   PlayerId playerId)
{
    // Base components (GlobalEntityId, Dirty, ChunkMembership, PendingCreate)
    // Chunk is computed from position inside BaseEntity::spawn()
    const entt::entity ent = BaseEntity::spawn(reg, globalId, x, y, z);

    // Player-specific components
    reg.emplace<DynamicPositionComponent>(ent, x, y, z, 0, 0, 0, /*grounded=*/false);
    reg.emplace<EntityTypeComponent>(ent, EntityType::PLAYER);
    reg.emplace<InputComponent>(ent);
    reg.emplace<PlayerComponent>(ent, playerId);
    reg.emplace<BoundingBoxComponent>(ent, PLAYER_BBOX_HX, PLAYER_BBOX_HY, PLAYER_BBOX_HZ);
    reg.emplace<PhysicsModeComponent>(ent, PhysicsMode::FULL);
    reg.emplace<JumpComponent>(ent);
    reg.emplace<WalkComponent>(ent);
    reg.emplace<HealthComponent>(ent, DEFAULT_HEALTH, DEFAULT_HEALTH, /*lastDamageTick=*/0);

    return ent;
}

entt::entity spawnImpl(entt::registry& reg,
                       GlobalEntityId globalId,
                       const EntitySpawnRequest& req)
{
    return spawn(reg, globalId, req.x, req.y, req.z, req.playerId);
}

size_t serializeCreate(entt::registry& reg, entt::entity ent, SafeBufWriter& w) {
    const size_t startOffset = w.offset();

    // For CREATE: component decides if it needs serialization based on non-default values
    uint8_t flags = 0;
    const auto& pos = reg.get<DynamicPositionComponent>(ent);
    if (pos.isNonDefault()) flags |= POSITION_BIT;

    const auto& health = reg.get<HealthComponent>(ent);
    if (health.isNonDefault()) flags |= HEALTH_BIT;

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::PLAYER));
    w.write(flags);

    // Serialize only if non-default
    if (flags & POSITION_BIT) {
        DynamicPositionComponent::serialize(reg, ent, POSITION_BIT, w);
    }
    if (flags & HEALTH_BIT) {
        HealthComponent::serialize(reg, ent, HEALTH_BIT, w);
    }

    return w.offset() - startOffset;
}

size_t serializeUpdate(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w) {
    const uint8_t flags = dirty.dirtyFlags & (POSITION_BIT | HEALTH_BIT);

    // Nothing to serialize if no tracked components are dirty
    if (flags == 0) {
        return 0;
    }

    const size_t startOffset = w.offset();

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::PLAYER));
    w.write(flags);
    DynamicPositionComponent::serialize(reg, ent, flags, w);
    HealthComponent::serialize(reg, ent, flags, w);

    return w.offset() - startOffset;
}

} // namespace voxelmmo::PlayerEntity
