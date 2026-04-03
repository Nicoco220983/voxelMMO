#pragma once
#include "game/ChunkRegistry.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"
#include "game/components/GroundContactComponent.hpp"
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include "common/VoxelPhysicProps.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

namespace PhysicsSystem {

/**
 * @brief Step physics for all entities with DynamicPositionComponent,
 *        BoundingBoxComponent, and PhysicsModeComponent.
 *
 * Iterates chunk-first so that the VoxelContext cache is pre-warmed with each
 * chunk, avoiding redundant hash-map lookups for entities near their chunk centre.
 * 
 * Note: Jump handling is performed by JumpSystem, which should run after PhysicsSystem.
 * PhysicsSystem only handles physics simulation (gravity, collision, bounce).
 */
void apply(entt::registry& registry, const ChunkRegistry& chunkRegistry);

} // namespace PhysicsSystem

} // namespace voxelmmo
