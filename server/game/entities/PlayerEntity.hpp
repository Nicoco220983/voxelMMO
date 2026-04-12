#pragma once
#include "BaseEntity.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/InputComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"
#include "common/NetworkProtocol.hpp"
#include "common/Types.hpp"
#include "common/VoxelPhysicProps.hpp"
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
struct PlayerEntityTag {};

} // namespace voxelmmo

namespace voxelmmo::PlayerEntity {

/**
 * @brief Full-physics player: gravity + swept AABB collision.
 *
 * Calls BaseEntity::spawn() first to initialize common components, then adds
 * player-specific components.
 *
 * The chunk is computed from position; no need to pass it separately.
 *
 * @param reg      Entity registry.
 * @param globalId Global entity ID (pre-acquired).
 * @param x,y,z    Spawn position in sub-voxels.
 * @param playerId Persistent player ID.
 * @return Entity handle.
 */
inline constexpr int32_t PLAYER_WALK_SPEED = 77;   ///<  6 vox/s × SUBVOXEL_SIZE × TICK_DT
inline constexpr int32_t PLAYER_JUMP_VY    = 90;   ///< gives ≈ 3.9 voxel jump height
inline constexpr uint16_t DEFAULT_HEALTH   = 100;  ///< Default player health points

/** @brief Players don't spawn naturally in the world. */
inline constexpr std::array<VoxelType, 0> SPAWNABLE_VOXELS = {};
inline constexpr float SPAWN_PROBABILITY_PER_VOXEL = 0.0f;

/**
 * @brief Compute player velocity from input buttons and yaw.
 * Horizontal-only: W/S/A/D ignore pitch; Space = jump impulse when grounded.
 * 
 * @param maxSpeedXZ Maximum horizontal speed (0 = use default PLAYER_WALK_SPEED)
 */
void computeVelocity(const InputComponent& inp, const DynamicPositionComponent& dyn, 
                     int32_t& nvx, int32_t& nvy, int32_t& nvz,
                     uint16_t maxSpeedXZ = 0);

entt::entity spawn(entt::registry& reg,
                   GlobalEntityId globalId,
                   SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z,
                   PlayerId playerId);

/**
 * @brief Spawn implementation for EntityFactory (uses EntitySpawnRequest).
 */
entt::entity spawnImpl(entt::registry& reg,
                       GlobalEntityId globalId,
                       const EntitySpawnRequest& req);

/**
 * @brief Serialize a full player entity for creation (no delta type prefix).
 *
 * Format: [global_id(4)] [entity_type(1)=PLAYER] [component_mask(1)] [position_data...]
 *
 * Player entities always include POSITION_BIT in the component mask.
 *
 * @param reg Entity registry.
 * @param ent Entity handle.
 * @param w   Buffer writer.
 * @return Bytes written.
 */
size_t serializeCreate(entt::registry& reg, entt::entity ent, SafeBufWriter& w);

/**
 * @brief Serialize player entity update (no delta type prefix).
 *
 * Only writes if POSITION_BIT is set in dirty flags.
 *
 * Format: [global_id(4)] [entity_type(1)=PLAYER] [component_mask(1)] [position_data... if POSITION_BIT]
 *
 * @param reg   Entity registry.
 * @param ent   Entity handle.
 * @param dirty DirtyComponent containing dirty flags.
 * @param w     Buffer writer.
 * @return Bytes written (0 if nothing dirty).
 */
size_t serializeUpdate(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w);

} // namespace voxelmmo::PlayerEntity

namespace voxelmmo {

// EntityTraits specialization for Player (must be after function declarations)
template<>
struct EntityTraits<PlayerEntityTag> {
    static constexpr uint8_t typeId = static_cast<uint8_t>(EntityType::PLAYER);
    static constexpr std::string_view name = "PLAYER";
    static constexpr auto serializeCreate = PlayerEntity::serializeCreate;
    static constexpr auto serializeUpdate = PlayerEntity::serializeUpdate;
    static constexpr auto spawnImpl = PlayerEntity::spawnImpl;
    static constexpr std::span<const VoxelType> spawnableVoxels = PlayerEntity::SPAWNABLE_VOXELS;
    static constexpr float spawnProbabilityPerVoxel = PlayerEntity::SPAWN_PROBABILITY_PER_VOXEL;
};

} // namespace voxelmmo
