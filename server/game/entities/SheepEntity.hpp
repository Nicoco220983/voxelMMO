#pragma once
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/ChunkMembershipComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"
#include "game/components/SheepBehaviorComponent.hpp"
#include "common/MessageTypes.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>

namespace voxelmmo::SheepEntity {

/** @brief Sheep dimensions in sub-voxels (slightly smaller than player). */
inline constexpr int32_t SHEEP_BBOX_HX = 128;  // 0.5 voxels
inline constexpr int32_t SHEEP_BBOX_HY = 128;  // 0.5 voxels
inline constexpr int32_t SHEEP_BBOX_HZ = 192;  // 0.75 voxels

/** @brief Walking speed in sub-voxels per tick. */
inline constexpr int32_t SHEEP_WALK_SPEED = 38;  // ~0.15 voxels/tick

/**
 * @brief Spawn a sheep entity.
 * @param reg      Entity registry.
 * @param ent      Pre-created entity handle.
 * @param x,y,z    Spawn position in sub-voxels.
 * @param chunkId  Initial chunk.
 * @param startTick Current server tick (for state timing).
 */
inline void spawn(entt::registry& reg, entt::entity ent,
                  int32_t x, int32_t y, int32_t z,
                  ChunkId chunkId, uint32_t startTick)
{
    reg.emplace<DynamicPositionComponent>(ent, x, y, z, 0, 0, 0, /*grounded=*/true);
    reg.emplace<DirtyComponent>(ent);
    reg.emplace<EntityTypeComponent>(ent, EntityType::SHEEP);
    reg.emplace<ChunkMembershipComponent>(ent, chunkId);
    reg.emplace<BoundingBoxComponent>(ent, SHEEP_BBOX_HX, SHEEP_BBOX_HY, SHEEP_BBOX_HZ);
    reg.emplace<PhysicsModeComponent>(ent, PhysicsMode::FULL);
    
    // Start in IDLE state for 2-5 seconds (120-300 ticks at 60tps)
    uint32_t idleTicks = 120 + (startTick % 180);  // 120-299 ticks
    reg.emplace<SheepBehaviorComponent>(ent, 
        SheepBehaviorComponent::State::IDLE,
        startTick + idleTicks,
        x, z, 0.0f);
}

} // namespace voxelmmo::SheepEntity
