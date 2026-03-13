#pragma once
#include "BaseEntity.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"
#include "game/components/SheepBehaviorComponent.hpp"
#include "common/MessageTypes.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {
struct EntitySpawnRequest;
struct DirtyComponent;
}

namespace voxelmmo::SheepEntity {

/** @brief Sheep dimensions in sub-voxels (slightly smaller than player). */
inline constexpr int32_t SHEEP_BBOX_HX = 128;  // 0.5 voxels
inline constexpr int32_t SHEEP_BBOX_HY = 128;  // 0.5 voxels
inline constexpr int32_t SHEEP_BBOX_HZ = 192;  // 0.75 voxels

/** @brief Walking speed in sub-voxels per tick. */
inline constexpr int32_t SHEEP_WALK_SPEED = 38;  // ~0.15 voxels/tick

/**
 * @brief Spawn a sheep entity.
 *
 * Calls BaseEntity::spawn() first to initialize common components, then adds
 * sheep-specific components (AI behavior, smaller bounding box).
 *
 * The chunk is computed from position; no need to pass it separately.
 *
 * @param reg       Entity registry.
 * @param globalId  Global entity ID (pre-acquired).
 * @param x,y,z     Spawn position in sub-voxels.
 * @param startTick Current server tick (for AI state timing).
 * @return Entity handle.
 */
inline entt::entity spawn(entt::registry& reg,
                          GlobalEntityId globalId,
                          SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z,
                          uint32_t startTick)
{
    // Base components (GlobalEntityId, Dirty, ChunkMembership, PendingCreate)
    // Chunk is computed from position inside BaseEntity::spawn()
    const entt::entity ent = BaseEntity::spawn(reg, globalId, x, y, z);

    // Sheep-specific components
    reg.emplace<DynamicPositionComponent>(ent, x, y, z, 0, 0, 0, /*grounded=*/true);
    reg.emplace<EntityTypeComponent>(ent, EntityType::SHEEP);
    reg.emplace<BoundingBoxComponent>(ent, SHEEP_BBOX_HX, SHEEP_BBOX_HY, SHEEP_BBOX_HZ);
    reg.emplace<PhysicsModeComponent>(ent, PhysicsMode::FULL);

    // Start in IDLE state for 2-5 seconds (120-300 ticks at 60tps)
    uint32_t idleTicks = 120 + (startTick % 180);  // 120-299 ticks
    reg.emplace<SheepBehaviorComponent>(ent,
        SheepBehaviorComponent::State::IDLE,
        startTick + idleTicks,
        x, z, 0.0f);

    return ent;
}

/**
 * @brief Spawn implementation for EntityFactory (uses EntitySpawnRequest).
 */
entt::entity spawnImpl(entt::registry& reg,
                       GlobalEntityId globalId,
                       const EntitySpawnRequest& req);

/**
 * @brief Serialize a full sheep entity (no delta type prefix).
 *
 * Format: [global_id(4)] [entity_type(1)=SHEEP] [component_mask(1)] [position_data...] [behavior_data...]
 *
 * Sheep entities include POSITION_BIT and SHEEP_BEHAVIOR_BIT in the component mask.
 *
 * @param reg Entity registry.
 * @param ent Entity handle.
 * @param w   Buffer writer.
 * @return Bytes written.
 */
inline size_t serializeFull(entt::registry& reg, entt::entity ent, SafeBufWriter& w) {
    const size_t startOffset = w.offset();

    // Sheep has POSITION_BIT and SHEEP_BEHAVIOR_BIT
    constexpr uint8_t flags = POSITION_BIT | SHEEP_BEHAVIOR_BIT;

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::SHEEP));
    w.write(flags);
    DynamicPositionComponent::serialize(reg, ent, flags, w);

    // Serialize sheep behavior component
    const auto& behavior = reg.get<SheepBehaviorComponent>(ent);
    behavior.serializeFields(w);

    return w.offset() - startOffset;
}

/**
 * @brief Serialize sheep entity update (no delta type prefix).
 *
 * Writes components based on dirty flags (POSITION_BIT and/or SHEEP_BEHAVIOR_BIT).
 *
 * Format: [global_id(4)] [entity_type(1)=SHEEP] [component_mask(1)] [position_data... if POSITION_BIT] [behavior_data... if SHEEP_BEHAVIOR_BIT]
 *
 * @param reg   Entity registry.
 * @param ent   Entity handle.
 * @param dirty DirtyComponent containing dirty flags.
 * @param w     Buffer writer.
 * @return Bytes written (0 if nothing dirty).
 */
inline size_t serializeUpdate(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w) {
    // Sheep tracks both position and behavior
    const uint8_t flags = dirty.dirtyFlags & (POSITION_BIT | SHEEP_BEHAVIOR_BIT);

    // Nothing to serialize if no tracked components are dirty
    if (flags == 0) {
        return 0;
    }

    const size_t startOffset = w.offset();

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::SHEEP));
    w.write(flags);
    DynamicPositionComponent::serialize(reg, ent, flags, w);

    // Serialize sheep behavior component if dirty
    if (flags & SHEEP_BEHAVIOR_BIT) {
        const auto& behavior = reg.get<SheepBehaviorComponent>(ent);
        behavior.serializeFields(w);
    }

    return w.offset() - startOffset;
}

} // namespace voxelmmo::SheepEntity
