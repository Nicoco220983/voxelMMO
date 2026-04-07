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
#include <entt/entt.hpp>
#include <cmath>

namespace voxelmmo {
struct EntitySpawnRequest;
struct DirtyComponent;
}

namespace voxelmmo::GhostPlayerEntity {

/**
 * @brief Ghost player: velocity-only, no gravity, no collision. Always grounded.
 *
 * Calls BaseEntity::spawn() first to initialize common components, then adds
 * ghost-specific components.
 *
 * The chunk is computed from position; no need to pass it separately.
 *
 * @param reg      Entity registry.
 * @param globalId Global entity ID (pre-acquired).
 * @param x,y,z    Spawn position in sub-voxels.
 * @param playerId Persistent player ID.
 * @return Entity handle.
 */
inline constexpr int32_t GHOST_MOVE_SPEED = 256;  ///< 20 vox/s × SUBVOXEL_SIZE × TICK_DT

/**
 * @brief Compute ghost player velocity from input buttons, yaw and pitch.
 */
inline void computeVelocity(const InputComponent& inp, int32_t& nvx, int32_t& nvy, int32_t& nvz) {
    const uint8_t b = inp.buttons;
    const float cy = std::cos(inp.yaw),   sy = std::sin(inp.yaw);
    const float cp = std::cos(inp.pitch),  sp = std::sin(inp.pitch);
    float dx = 0, dy = 0, dz = 0;
    if (b & static_cast<uint8_t>(InputButton::FORWARD))  { dx += -sy*cp; dy += sp; dz += -cy*cp; }
    if (b & static_cast<uint8_t>(InputButton::BACKWARD)) { dx -= -sy*cp; dy -= sp; dz -= -cy*cp; }
    if (b & static_cast<uint8_t>(InputButton::LEFT))     { dx -= cy;              dz -= -sy;     }
    if (b & static_cast<uint8_t>(InputButton::RIGHT))    { dx += cy;              dz += -sy;     }
    if (b & static_cast<uint8_t>(InputButton::JUMP))     { dy += 1.0f; }
    if (b & static_cast<uint8_t>(InputButton::DESCEND))  { dy -= 1.0f; }
    const float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    const float s   = (len > 0.001f) ? (static_cast<float>(GHOST_MOVE_SPEED) / len) : 0.0f;
    nvx = static_cast<int32_t>(dx * s);
    nvy = static_cast<int32_t>(dy * s);
    nvz = static_cast<int32_t>(dz * s);
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
 * @brief Serialize a full ghost player entity for creation (no delta type prefix).
 *
 * Format: [global_id(4)] [entity_type(1)=GHOST_PLAYER] [component_mask(1)] [position_data...]
 *
 * Ghost player entities always include POSITION_BIT in the component mask.
 *
 * @param reg Entity registry.
 * @param ent Entity handle.
 * @param w   Buffer writer.
 * @return Bytes written.
 */
size_t serializeCreate(entt::registry& reg, entt::entity ent, SafeBufWriter& w);

/**
 * @brief Serialize ghost player entity update (no delta type prefix).
 *
 * Only writes if POSITION_BIT is set in dirty flags.
 *
 * Format: [global_id(4)] [entity_type(1)=GHOST_PLAYER] [component_mask(1)] [position_data... if POSITION_BIT]
 *
 * @param reg   Entity registry.
 * @param ent   Entity handle.
 * @param dirty DirtyComponent containing dirty flags.
 * @param w     Buffer writer.
 * @return Bytes written (0 if nothing dirty).
 */
size_t serializeUpdate(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w);

} // namespace voxelmmo::GhostPlayerEntity
