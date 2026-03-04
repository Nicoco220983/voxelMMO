#pragma once
#include "common/Types.hpp"
#include "game/WorldGenerator.hpp"
#include "game/Chunk.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/PendingChunkChangeComponent.hpp"
#include "game/components/ChunkMembershipComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>
#include <vector>
#include <set>

namespace voxelmmo {

/**
 * @brief Unified entity lifecycle management system.
 *
 * Processes all entity state changes marked via DirtyComponent:
 * - CREATE: New entities added to chunks (must have PendingCreateComponent)
 * - DELETE: Entities marked for deletion (PendingDeleteComponent)
 * - CHUNK_CHANGE: Entities moved between chunks (PendingChunkChangeComponent)
 *
 * Called once per tick after all game systems, before serialization.
 * Execution order ensures consistency:
 *   1. Process chunk changes (move between chunks, mark CREATED in new chunk)
 *   2. Process creations (add new entities to chunks)
 *   3. Collect deletions (entities to remove after serialization)
 *
 * Important: Entities marked for deletion are NOT destroyed immediately.
 * They remain in the chunk's entity set so their DELETE delta can be serialized.
 * Call destroyPendingDeletions() after serialization to actually destroy them.
 */
namespace EntityStateSystem {

/**
 * @brief Component to track pending entity creation.
 *
 * Entities with this component will be added to the specified chunk
 * during EntityStateSystem::apply().
 */
struct PendingCreateComponent {
    ChunkId targetChunkId;
    explicit PendingCreateComponent(ChunkId chunkId) : targetChunkId(chunkId) {}
};

/**
 * @brief Component to mark entity for deferred deletion.
 *
 * Entities with this component will be removed from their chunk
 * and returned in the deletion list during EntityStateSystem::apply().
 */
struct PendingDeleteComponent {};

/**
 * @brief Result of EntityStateSystem::apply() containing statistics and pending deletions.
 */
struct EntityStateResult {
    size_t created = 0;
    size_t deleted = 0;
    size_t chunkChanged = 0;
    std::vector<entt::entity> entitiesToDestroy;  ///< Entities to destroy after serialization
};

/**
 * @brief Process all pending entity lifecycle events.
 *
 * NOTE: Entities marked for deletion are NOT destroyed by this function.
 * They are returned in result.entitiesToDestroy and must be destroyed
 * AFTER serialization by calling destroyPendingDeletions().
 *
 * @param registry  The ECS registry.
 * @param chunks    Map of loaded chunks (may activate new chunks on CHUNK_CHANGE).
 * @param tickCount Current server tick.
 * @param generator WorldGenerator for terrain generation when activating new chunks.
 * @return Statistics about processed entities + list of entities to destroy.
 */
EntityStateResult apply(
    entt::registry& registry,
    std::unordered_map<ChunkId, std::unique_ptr<Chunk>>& chunks,
    int32_t tickCount,
    const WorldGenerator& generator);

/**
 * @brief Destroy all entities that were marked for deletion.
 *
 * Call this AFTER serialization to ensure DELETE deltas are sent.
 *
 * @param registry       The ECS registry.
 * @param entitiesToDestroy List of entities returned from apply().
 */
void destroyPendingDeletions(entt::registry& registry, std::vector<entt::entity>& entitiesToDestroy);

/**
 * @brief Mark an entity for creation in the specified chunk.
 *
 * The entity will be added to the chunk during the next apply() call.
 * This should be called immediately after registry.create().
 *
 * @param registry  The ECS registry.
 * @param ent       The entity to mark.
 * @param chunkId   Target chunk for the entity.
 */
void markForCreation(entt::registry& registry, entt::entity ent, ChunkId chunkId);

/**
 * @brief Mark an entity for deletion at end of tick.
 *
 * Prefer this over direct registry.destroy() to ensure proper
 * cleanup and network synchronization.
 *
 * @param registry  The ECS registry.
 * @param ent       The entity to mark for deletion.
 */
void markForDeletion(entt::registry& registry, entt::entity ent);

/**
 * @brief Mark an entity for chunk change.
 *
 * Called by ChunkMembershipSystem when entity crosses chunk boundary.
 * The actual move happens during apply().
 *
 * @param registry   The ECS registry.
 * @param ent        The entity to move.
 * @param newChunkId The destination chunk.
 */
void markForChunkChange(entt::registry& registry, entt::entity ent, ChunkId newChunkId);

/**
 * @brief Get or activate a chunk, adding it to the activation list.
 * @param generator WorldGenerator for terrain generation (stateless).
 */
inline Chunk& activateChunk(
    ChunkId cid,
    std::unordered_map<ChunkId, std::unique_ptr<Chunk>>& chunks,
    std::vector<ChunkId>& activatedOut,
    const WorldGenerator& generator)
{
    auto it = chunks.find(cid);
    if (it == chunks.end()) {
        auto chunk = std::make_unique<Chunk>(cid);
        generator.generate(chunk->world.voxels, cid.x(), cid.y(), cid.z());
        Chunk* ptr = chunk.get();
        chunks[cid] = std::move(chunk);
        activatedOut.push_back(cid);
        return *ptr;
    }
    return *it->second;
}

} // namespace EntityStateSystem

} // namespace voxelmmo
