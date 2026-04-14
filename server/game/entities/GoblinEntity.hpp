#pragma once
#include "BaseEntity.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"
#include "game/components/GoblinBehaviorComponent.hpp"
#include "common/NetworkProtocol.hpp"
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include <entt/entt.hpp>
#include <array>
#include <span>

namespace voxelmmo {
struct EntitySpawnRequest;
struct DirtyComponent;

// Forward declare EntityTraits template from EntityCatalog.hpp
template<typename T> struct EntityTraits;

// Tag type for EntityTraits specialization
struct GoblinEntityTag {};

} // namespace voxelmmo

namespace voxelmmo::GoblinEntity {

/** @brief Goblin dimensions in sub-voxels (smaller than player).
 * HY is larger than visual height to make goblins easier to hit in combat.
 * The collision box extends above the visual model.
 */
inline constexpr int32_t GOBLIN_BBOX_HX = 115;  // 0.45 voxels
inline constexpr int32_t GOBLIN_BBOX_HY = 179;  // 0.7 voxels (taller for easier targeting)
inline constexpr int32_t GOBLIN_BBOX_HZ = 115;  // 0.45 voxels

/** @brief Walking speed when wandering (sub-voxels per tick). */
inline constexpr int32_t GOBLIN_WALK_SPEED = 20;   // ~0.12 voxels/tick

/** @brief Chasing speed when pursuing player (sub-voxels per tick). */
inline constexpr int32_t GOBLIN_CHASE_SPEED = 50;  // ~0.30 voxels/tick (player speed)

/** @brief Aggro detection radius in sub-voxels. */
inline constexpr int32_t GOBLIN_AGGRO_RADIUS = 10 * SUBVOXEL_SIZE;  // 10 voxels

/** @brief Attack range in sub-voxels. */
inline constexpr int32_t GOBLIN_ATTACK_RADIUS = 2 * SUBVOXEL_SIZE;  // 2 voxels

/** @brief Damage per attack. */
inline constexpr uint16_t GOBLIN_ATTACK_DAMAGE = 5;

/** @brief Knockback impulse (sub-voxels/tick). */
inline constexpr float GOBLIN_KNOCKBACK = 150.0f;

/** @brief Default health for goblin entities. */
inline constexpr uint16_t DEFAULT_HEALTH = 30;

/** @brief Time between attacks (ticks at 20tps = 3s). */
inline constexpr uint32_t ATTACK_COOLDOWN_TICKS = 60;

/** @brief Time before dropping aggro when target out of range (ticks at 20tps = 1s). */
inline constexpr uint32_t AGGRO_TIMEOUT_TICKS = 20;

/** @brief Voxel types goblins can spawn on. */
inline constexpr std::array<VoxelType, 1> SPAWNABLE_VOXELS = {VoxelTypes::DIRT};

/** @brief Spawn probability per candidate voxel (0.1% = 0.001f). */
inline constexpr float SPAWN_PROBABILITY_PER_VOXEL = 0.001f;

/**
 * @brief Spawn a goblin entity.
 *
 * Calls BaseEntity::spawn() first to initialize common components, then adds
 * goblin-specific components (AI behavior, smaller bounding box).
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
 * @brief Serialize a full goblin entity for creation.
 *
 * Format: [global_id(4)] [entity_type(1)=GOBLIN] [component_mask(1)] [position_data...] [behavior_data...] [health_data...]
 *
 * @param reg Entity registry.
 * @param ent Entity handle.
 * @param w   Buffer writer.
 * @return Bytes written.
 */
size_t serializeCreate(entt::registry& reg, entt::entity ent, SafeBufWriter& w);

/**
 * @brief Serialize goblin entity update.
 *
 * Writes components based on dirty flags (POSITION_BIT, AI_BEHAVIOR_BIT, HEALTH_BIT).
 *
 * @param reg   Entity registry.
 * @param ent   Entity handle.
 * @param dirty DirtyComponent containing dirty flags.
 * @param w     Buffer writer.
 * @return Bytes written (0 if nothing dirty).
 */
size_t serializeUpdate(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w);

} // namespace voxelmmo::GoblinEntity

namespace voxelmmo {

// EntityTraits specialization for Goblin (must be after function declarations)
template<>
struct EntityTraits<GoblinEntityTag> {
    static constexpr uint8_t typeId = static_cast<uint8_t>(EntityType::GOBLIN);
    static constexpr std::string_view name = "GOBLIN";
    static constexpr auto serializeCreate = GoblinEntity::serializeCreate;
    static constexpr auto serializeUpdate = GoblinEntity::serializeUpdate;
    static constexpr auto spawnImpl = GoblinEntity::spawnImpl;
    static constexpr std::span<const VoxelType> spawnableVoxels = GoblinEntity::SPAWNABLE_VOXELS;
    static constexpr float spawnProbabilityPerVoxel = GoblinEntity::SPAWN_PROBABILITY_PER_VOXEL;
};

} // namespace voxelmmo
