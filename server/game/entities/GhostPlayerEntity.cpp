#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/EntityFactory.hpp"
#include "game/components/ToolComponent.hpp"
#include "common/EntityCatalog.hpp"

namespace voxelmmo::GhostPlayerEntity {

// Auto-registration in EntityCatalog
namespace {
    [[maybe_unused]] const EntityRegistrar<GhostPlayerEntityTag>& _ghostReg = 
        EntityRegistrar<GhostPlayerEntityTag>{};
}

entt::entity spawn(entt::registry& reg,
                   GlobalEntityId globalId,
                   SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z,
                   PlayerId playerId)
{
    // Base components (GlobalEntityId, Dirty, ChunkMembership, PendingCreate)
    // Chunk is computed from position inside BaseEntity::spawn()
    const entt::entity ent = BaseEntity::spawn(reg, globalId, x, y, z);

    // Ghost-specific components
    reg.emplace<DynamicPositionComponent>(ent, x, y, z, 0, 0, 0, /*grounded=*/true);
    reg.emplace<EntityTypeComponent>(ent, EntityType::GHOST_PLAYER);
    reg.emplace<InputComponent>(ent);
    reg.emplace<PlayerComponent>(ent, playerId);
    reg.emplace<BoundingBoxComponent>(ent, PLAYER_BBOX_HX, PLAYER_BBOX_HY, PLAYER_BBOX_HZ);
    reg.emplace<PhysicsModeComponent>(ent, PhysicsMode::GHOST);
    reg.emplace<ToolComponent>(ent);  // Default: HAND tool

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

    const auto* tool = reg.try_get<ToolComponent>(ent);
    if (tool && tool->isNonDefault()) flags |= TOOL_BIT;

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::GHOST_PLAYER));
    w.write(flags);

    // Serialize only if non-default
    if (flags & POSITION_BIT) {
        DynamicPositionComponent::serialize(reg, ent, POSITION_BIT, w);
    }
    if (flags & TOOL_BIT) {
        tool->serializeFields(w);
    }

    return w.offset() - startOffset;
}

size_t serializeUpdate(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w) {
    const uint8_t flags = dirty.dirtyFlags & (POSITION_BIT | TOOL_BIT);

    // Nothing to serialize if no tracked components are dirty
    if (flags == 0) {
        return 0;
    }

    const size_t startOffset = w.offset();

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::GHOST_PLAYER));
    w.write(flags);
    DynamicPositionComponent::serialize(reg, ent, flags, w);
    if (flags & TOOL_BIT) {
        if (const auto* tool = reg.try_get<ToolComponent>(ent)) {
            tool->serializeFields(w);
        }
    }

    return w.offset() - startOffset;
}

} // namespace voxelmmo::GhostPlayerEntity
