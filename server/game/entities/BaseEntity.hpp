#pragma once
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

/**
 * @brief Compute ChunkId from sub-voxel position.
 *
 * Convenience wrapper around chunkIdOf() for use in entity spawn functions.
 * The chunk is computed from position using arithmetic right-shift (works
 * correctly for negative coordinates).
 */
inline constexpr ChunkId chunkIdFromPosition(int32_t x, int32_t y, int32_t z) noexcept {
    return chunkIdOf(x, y, z);
}

} // namespace voxelmmo

namespace voxelmmo {

/**
 * @brief Base entity spawning functionality.
 *
 * All entity spawn methods should call BaseEntity::spawn() first to ensure
 * common components are properly initialized:
 * - GlobalEntityIdComponent: stable wire ID
 * - DirtyComponent: lifecycle and change tracking
 * - DirtyComponent::markCreated(): lifecycle tracking for network sync (CREATED_BIT)
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
     * The chunk is computed from position using chunkIdFromPosition().
     *
     * @param reg      Entity registry.
     * @param globalId Global entity ID (acquired from GameEngine::acquireEntityId()).
     * @param x,y,z    Entity position in sub-voxels (used to compute chunk).
     * @return Entity handle with base components already assigned.
     */
    static entt::entity spawn(entt::registry& reg,
                              GlobalEntityId globalId,
                              int32_t x, int32_t y, int32_t z) {
        const entt::entity ent = reg.create();

        // Core wire identification - never changes
        reg.emplace<GlobalEntityIdComponent>(ent, globalId);

        // Dirty tracking for lifecycle (CREATED/DELETED) and component changes
        reg.emplace<DirtyComponent>(ent);

        // Mark entity as newly created for network sync
        // Chunk is determined from position during tick processing
        markForCreation(reg, ent);

        return ent;
    }


    /**
    * @brief Mark an entity as newly created for network synchronization.
    *
    * Sets the CREATED_BIT in DirtyComponent so the entity's initial state
    * is sent to watching clients. The entity will be added to its chunk
    * during the next tick based on its position.
    *
    * @param registry  The ECS registry.
    * @param ent       The entity to mark.
    */
    static inline void markForCreation(entt::registry& registry, entt::entity ent) {
        registry.get<DirtyComponent>(ent).markCreated();
    }
};

} // namespace voxelmmo
