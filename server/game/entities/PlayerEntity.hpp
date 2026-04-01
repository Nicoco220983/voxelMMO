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
 */
inline void computeVelocity(const InputComponent& inp, const DynamicPositionComponent& dyn, int32_t& nvx, int32_t& nvy, int32_t& nvz) {
    const uint8_t b = inp.buttons;
    const float cy = std::cos(inp.yaw), sy = std::sin(inp.yaw);
    float dx = 0, dz = 0;
    if (b & static_cast<uint8_t>(InputButton::FORWARD))  { dx += -sy; dz += -cy; }
    if (b & static_cast<uint8_t>(InputButton::BACKWARD)) { dx -= -sy; dz -= -cy; }
    if (b & static_cast<uint8_t>(InputButton::LEFT))     { dx -= cy;  dz -= -sy; }
    if (b & static_cast<uint8_t>(InputButton::RIGHT))    { dx += cy;  dz += -sy; }
    const float hlen = std::sqrt(dx*dx + dz*dz);
    const float hs   = (hlen > 0.001f) ? (static_cast<float>(PLAYER_WALK_SPEED) / hlen) : 0.0f;
    nvx = static_cast<int32_t>(dx * hs);
    nvz = static_cast<int32_t>(dz * hs);
    // Jump: impulse when grounded; vy otherwise owned by physics (gravity)
    if ((b & static_cast<uint8_t>(InputButton::JUMP)) && dyn.grounded)
        nvy = PLAYER_JUMP_VY;
}

inline entt::entity spawn(entt::registry& reg,
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

    return ent;
}

/**
 * @brief Spawn implementation for EntityFactory (uses EntitySpawnRequest).
 */
entt::entity spawnImpl(entt::registry& reg,
                       GlobalEntityId globalId,
                       const EntitySpawnRequest& req);

/**
 * @brief Serialize a full player entity (no delta type prefix).
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
inline size_t serializeFull(entt::registry& reg, entt::entity ent, SafeBufWriter& w) {
    const size_t startOffset = w.offset();

    // Player has only POSITION_BIT
    constexpr uint8_t flags = POSITION_BIT;

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::PLAYER));
    w.write(flags);
    DynamicPositionComponent::serialize(reg, ent, flags, w);

    return w.offset() - startOffset;
}

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
inline size_t serializeUpdate(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w) {
    const uint8_t flags = dirty.dirtyFlags & POSITION_BIT;

    // Nothing to serialize if position hasn't changed
    if (flags == 0) {
        return 0;
    }

    const size_t startOffset = w.offset();

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::PLAYER));
    w.write(flags);
    DynamicPositionComponent::serialize(reg, ent, flags, w);

    return w.offset() - startOffset;
}

} // namespace voxelmmo::PlayerEntity
