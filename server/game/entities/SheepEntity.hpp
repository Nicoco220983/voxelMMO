#pragma once
#include "BaseEntity.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"
#include "game/components/SheepBehaviorComponent.hpp"
#include "common/NetworkProtocol.hpp"
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

/** @brief Default health for sheep entities. */
inline constexpr uint16_t DEFAULT_HEALTH = 20;

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
entt::entity spawn(entt::registry& reg,
                   GlobalEntityId globalId,
                   SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z,
                   uint32_t startTick);

/**
 * @brief Spawn implementation for EntityFactory (uses EntitySpawnRequest).
 */
entt::entity spawnImpl(entt::registry& reg,
                       GlobalEntityId globalId,
                       const EntitySpawnRequest& req);

/**
 * @brief Serialize a full sheep entity for creation (no delta type prefix).
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
size_t serializeCreate(entt::registry& reg, entt::entity ent, SafeBufWriter& w);

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
size_t serializeUpdate(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w);

} // namespace voxelmmo::SheepEntity
