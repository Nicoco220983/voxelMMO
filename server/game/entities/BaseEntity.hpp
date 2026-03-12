#pragma once
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/ChunkMembershipComponent.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

/**
 * @brief Base entity spawning functionality.
 *
 * All entity spawn methods should call BaseEntity::spawn() first to ensure
 * common components are properly initialized:
 * - GlobalEntityIdComponent: stable wire ID
 * - DirtyComponent: lifecycle and change tracking
 * - DirtyComponent::markCreated(): lifecycle tracking for network sync (CREATE_ENTITY delta type)
 *
 * This centralizes entity creation and eliminates duplicate code in GameEngine
 * and WorldGenerator.
 */
struct BaseEntity {
    /**
     * @brief Spawn base components for a new entity.
     *
     * This is the SINGLE place where GlobalEntityIdComponent and DirtyComponent
     * are assigned. All entity spawn methods must call this before adding
     * type-specific components.
     *
     * @param reg      Entity registry.
     * @param globalId Global entity ID (acquired from GameEngine::acquireEntityId()).
     * @param x,y,z    Entity position in sub-voxels (used to compute chunk).
     * @return Entity handle with base components already assigned.
     */
    static entt::entity spawn(entt::registry& reg,
                              GlobalEntityId globalId,
                              SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z) {
        const entt::entity ent = reg.create();

        // Core wire identification - never changes
        reg.emplace<GlobalEntityIdComponent>(ent, globalId);

        // Dirty tracking for lifecycle (CREATE_ENTITY delta type) and component changes
        reg.emplace<DirtyComponent>(ent);
        // Mark entity as newly created for network sync
        reg.get<DirtyComponent>(ent).markCreated();

        // Chunk membership - assigned at spawn, updated by ChunkMembershipSystem
        const ChunkId chunkId = ChunkId::fromSubVoxelPos(x, y, z);
        reg.emplace<ChunkMembershipComponent>(ent, chunkId);

        return ent;
    }
};

} // namespace voxelmmo
