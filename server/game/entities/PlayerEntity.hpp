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
#include <entt/entt.hpp>
#include <cmath>

namespace voxelmmo {
struct EntitySpawnRequest;
struct DirtyComponent;
}

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

/**
 * @brief Compute player velocity from input buttons and yaw.
 * Horizontal-only: W/S/A/D ignore pitch; Space = jump impulse when grounded.
 * 
 * @param maxSpeedXZ Maximum horizontal speed (0 = use default PLAYER_WALK_SPEED)
 */
inline void computeVelocity(const InputComponent& inp, const DynamicPositionComponent& dyn, 
                            int32_t& nvx, int32_t& nvy, int32_t& nvz,
                            uint16_t maxSpeedXZ = 0) {
    const uint8_t b = inp.buttons;
    const float cy = std::cos(inp.yaw), sy = std::sin(inp.yaw);
    float dx = 0, dz = 0;
    if (b & static_cast<uint8_t>(InputButton::FORWARD))  { dx += -sy; dz += -cy; }
    if (b & static_cast<uint8_t>(InputButton::BACKWARD)) { dx -= -sy; dz -= -cy; }
    if (b & static_cast<uint8_t>(InputButton::LEFT))     { dx -= cy;  dz -= -sy; }
    if (b & static_cast<uint8_t>(InputButton::RIGHT))    { dx += cy;  dz += -sy; }
    
    // Use surface-specific max speed if provided, otherwise default
    const int32_t speedLimit = (maxSpeedXZ > 0) ? maxSpeedXZ : PLAYER_WALK_SPEED;
    
    const float hlen = std::sqrt(dx*dx + dz*dz);
    const float hs   = (hlen > 0.001f) ? (static_cast<float>(speedLimit) / hlen) : 0.0f;
    nvx = static_cast<int32_t>(dx * hs);
    nvz = static_cast<int32_t>(dz * hs);
    // Note: Jump is now handled in PhysicsSystem (auto-jump with bounce momentum)
    // nvy is not modified here - PhysicsSystem handles vertical velocity
}

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
