#pragma once
#include "game/ChunkRegistry.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/ChunkMembershipComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/PendingChunkChangeComponent.hpp"
#include "game/components/PendingCreateComponent.hpp"
#include "game/components/PendingDeleteComponent.hpp"
#include "common/Types.hpp"
#include "common/GatewayInfo.hpp"
#include "common/NetworkProtocol.hpp"
#include <entt/entt.hpp>
#include <vector>
#include <set>

namespace voxelmmo {

// ── Result structures ─────────────────────────────────────────────────────────

/**
 * @brief Result of entity chunk operations.
 */
struct EntityChunkResult {
    size_t created = 0;
    size_t deleted = 0;
    size_t chunkChanged = 0;
    std::vector<entt::entity> entitiesToDestroy;  ///< Entities to destroy after serialization
};

/**
 * @brief Result of detectChunkCrossings() containing detected crossings.
 */
struct ChunkCrossingResult {
    /** Chunks that were activated (created) during the update. */
    std::vector<ChunkId> activatedChunks;
};

// ── Forward declarations ──────────────────────────────────────────────────────

namespace ChunkMembershipSystem {

// Forward declarations for helper functions (defined after main functions)
inline void markForChunkChange(entt::registry& registry, entt::entity ent, ChunkId newChunkId);

/**
 * @brief Mark an entity for creation in the specified chunk.
 *
 * The entity will be added to the chunk during the next updateEntitiesChunks() call.
 * This should be called immediately after registry.create().
 *
 * @param registry  The ECS registry.
 * @param ent       The entity to mark.
 * @param chunkId   Target chunk for the entity.
 */
inline void markForCreation(entt::registry& registry, entt::entity ent, ChunkId chunkId) {
    registry.emplace<PendingCreateComponent>(ent, chunkId);
    registry.get<DirtyComponent>(ent).markCreated();
}

/**
 * @brief Mark an entity for deletion at end of tick.
 *
 * Prefer this over direct registry.destroy() to ensure proper
 * cleanup and network synchronization.
 *
 * @param registry  The ECS registry.
 * @param ent       The entity to mark for deletion.
 */
inline void markForDeletion(entt::registry& registry, entt::entity ent) {
    // Avoid double-marking
    if (!registry.all_of<PendingDeleteComponent>(ent)) {
        registry.emplace<PendingDeleteComponent>(ent);
        registry.get<DirtyComponent>(ent).markDeleted();
    }
}

/**
 * @brief Detect entities that have crossed chunk boundaries.
 *
 * Phase A: For entities with `moved=true`, compute new chunk from position,
 * and mark for chunk change via PendingChunkChangeComponent. The actual move
 * happens later in updateEntitiesChunks().
 *
 * @param registry          The ECS registry.
 * @param chunkRegistry     Chunk registry for accessing chunks.
 * @param tickCount         Current server tick.
 * @param activationRadius  Radius for chunk activation around players.
 * @return Result containing activated chunks.
 */
inline ChunkCrossingResult detectChunkCrossings(
    entt::registry& registry,
    ChunkRegistry& chunkRegistry,
    int32_t tickCount,
    int32_t activationRadius)
{
    (void)tickCount;
    ChunkCrossingResult result;

    auto view = registry.view<DynamicPositionComponent, ChunkMembershipComponent>();
    view.each([&](entt::entity ent, DynamicPositionComponent& dyn, ChunkMembershipComponent& cm) {
        if (!dyn.moved) return;
        dyn.moved = false;

        const int32_t cx = dyn.x >> CHUNK_SHIFT_X;
        const int32_t cy = dyn.y >> CHUNK_SHIFT_Y;
        const int32_t cz = dyn.z >> CHUNK_SHIFT_Z;
        const ChunkId newChunk = ChunkId::make(cy, cx, cz);

        if (newChunk == cm.currentChunkId) return;

        const bool isPlayer = registry.all_of<PlayerComponent>(ent);
        const PlayerId pid = isPlayer ? registry.get<PlayerComponent>(ent).playerId : 0u;

        // Remove from old chunk lists immediately (watchingPlayers only)
        const int32_t ocx = cm.currentChunkId.x();
        const int32_t ocy = cm.currentChunkId.y();
        const int32_t ocz = cm.currentChunkId.z();

        if (isPlayer) {
            for (int32_t dx = -activationRadius; dx <= activationRadius; ++dx)
            for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dz = -activationRadius; dz <= activationRadius; ++dz) {
                const ChunkId cid = ChunkId::make(ocy + dy, ocx + dx, ocz + dz);
                if (Chunk* chunk = chunkRegistry.getChunkMutable(cid))
                    chunk->watchingPlayers.erase(pid);
            }
        }

        // Mark for chunk change - actual move happens in updateEntitiesChunks()
        markForChunkChange(registry, ent, newChunk);

        // For players, set up watching in new radius (chunk will be activated in Phase B)
        if (isPlayer) {
            for (int32_t dx = -activationRadius; dx <= activationRadius; ++dx)
            for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dz = -activationRadius; dz <= activationRadius; ++dz) {
                const ChunkId cid = ChunkId::make(cy + dy, cx + dx, cz + dz);
                if (Chunk* chunk = chunkRegistry.getChunkMutable(cid)) {
                    chunk->watchingPlayers.insert(pid);
                }
            }
        }
    });

    return result;
}

/**
 * @brief Process all pending entity chunk operations.
 *
 * Phase B: Handles CREATE, DELETE, and CHUNK_CHANGE operations.
 * Entities marked for deletion are NOT destroyed immediately - they're returned
 * in result.entitiesToDestroy and must be destroyed AFTER serialization.
 *
 * @param registry      The ECS registry.
 * @param chunkRegistry Chunk registry (may activate new chunks on CHUNK_CHANGE).
 * @param tickCount     Current server tick.
 * @param generator     WorldGenerator for terrain generation when activating new chunks.
 * @return Statistics about processed entities + list of entities to destroy.
 */
inline EntityChunkResult updateEntitiesChunks(
    entt::registry& registry,
    ChunkRegistry& chunkRegistry,
    int32_t tickCount,
    WorldGenerator& generator)
{
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    EntityChunkResult result;

    // ========================================================================
    // Phase 1: Process chunk changes
    // ========================================================================
    {
        auto view = registry.view<PendingChunkChangeComponent, ChunkMembershipComponent, DirtyComponent>();
        std::vector<entt::entity> processed;
        processed.reserve(view.size_hint());

        for (auto ent : view) {
            auto& pcc = view.get<PendingChunkChangeComponent>(ent);
            auto& cm = view.get<ChunkMembershipComponent>(ent);

            const ChunkId oldChunkId = cm.currentChunkId;
            const ChunkId newChunkId = pcc.newChunkId;

            if (oldChunkId == newChunkId) {
                // No actual change, skip
                processed.push_back(ent);
                continue;
            }

            // Remove from old chunk
            if (Chunk* oldChunk = chunkRegistry.getChunkMutable(oldChunkId)) {
                oldChunk->entities.erase(ent);
            }

            // Ensure new chunk exists (activate if needed)
            Chunk* newChunk = chunkRegistry.activate(generator, newChunkId, registry, tick);

            // Add to new chunk
            newChunk->entities.insert(ent);

            // Update chunk membership
            cm.currentChunkId = newChunkId;

            // Mark as CREATED in new chunk (for viewers who haven't seen it)
            // The old chunk will send CHUNK_CHANGE delta (it still has the entity in its set)
            auto& dirty = view.get<DirtyComponent>(ent);
            dirty.markCreated();

            processed.push_back(ent);
            ++result.chunkChanged;
        }

        // Remove PendingChunkChangeComponent from processed entities
        for (auto ent : processed) {
            registry.remove<PendingChunkChangeComponent>(ent);
        }
    }

    // ========================================================================
    // Phase 2: Process creations
    // ========================================================================
    {
        auto view = registry.view<PendingCreateComponent, ChunkMembershipComponent, DirtyComponent>();
        std::vector<entt::entity> processed;
        processed.reserve(view.size_hint());

        for (auto ent : view) {
            auto& pcc = view.get<PendingCreateComponent>(ent);
            auto& cm = view.get<ChunkMembershipComponent>(ent);

            // Update chunk membership to target chunk
            cm.currentChunkId = pcc.targetChunkId;

            // Ensure chunk exists and add entity
            Chunk* chunk = chunkRegistry.activate(generator, pcc.targetChunkId, registry, tick);
            chunk->entities.insert(ent);

            // Add to present players if it's a player
            if (registry.all_of<PlayerComponent>(ent)) {
                const auto& pc = registry.get<PlayerComponent>(ent);
                chunk->presentPlayers.insert(pc.playerId);
            }

            processed.push_back(ent);
            ++result.created;
        }

        // Remove PendingCreateComponent from processed entities
        for (auto ent : processed) {
            registry.remove<PendingCreateComponent>(ent);
        }
    }

    // ========================================================================
    // Phase 3: Collect deletions (do NOT destroy yet - serialization needs them)
    // ========================================================================
    {
        auto view = registry.view<PendingDeleteComponent, ChunkMembershipComponent, DirtyComponent>();
        
        for (auto ent : view) {
            auto& cm = view.get<ChunkMembershipComponent>(ent);
            auto& dirty = view.get<DirtyComponent>(ent);

            // Remove from current chunk's entity set immediately
            // (so it won't be considered for future updates, but delta is already marked)
            if (Chunk* chunk = chunkRegistry.getChunkMutable(cm.currentChunkId)) {
                chunk->entities.erase(ent);

                // Remove from present players if it's a player
                if (registry.all_of<PlayerComponent>(ent)) {
                    const auto& pc = registry.get<PlayerComponent>(ent);
                    chunk->presentPlayers.erase(pc.playerId);
                }
            }

            // Mark dirty so the DELETE delta is sent this tick
            dirty.markDeleted();

            // Add to list for destruction after serialization
            result.entitiesToDestroy.push_back(ent);
            ++result.deleted;
        }
    }

    return result;
}

/**
 * @brief Destroy all entities that were marked for deletion.
 *
 * Call this AFTER serialization to ensure DELETE deltas are sent.
 *
 * @param registry            The ECS registry.
 * @param entitiesToDestroy   List of entities returned from updateEntitiesChunks().
 */
inline void destroyPendingDeletions(entt::registry& registry, std::vector<entt::entity>& entitiesToDestroy) {
    for (auto ent : entitiesToDestroy) {
        if (registry.valid(ent)) {
            registry.destroy(ent);
        }
    }
    entitiesToDestroy.clear();
}

/**
 * @brief Mark an entity for chunk change.
 *
 * Called by detectChunkCrossings() when entity crosses chunk boundary.
 * The actual move happens during updateEntitiesChunks().
 *
 * @param registry   The ECS registry.
 * @param ent        The entity to move.
 * @param newChunkId The destination chunk.
 */
inline void markForChunkChange(entt::registry& registry, entt::entity ent, ChunkId newChunkId) {
    // Update or add PendingChunkChangeComponent
    if (registry.all_of<PendingChunkChangeComponent>(ent)) {
        registry.get<PendingChunkChangeComponent>(ent).newChunkId = newChunkId;
    } else {
        registry.emplace<PendingChunkChangeComponent>(ent, newChunkId);
    }
}

/**
 * @brief Rebuild watched chunks for a gateway based on its players' positions.
 *
 * Phase C: Clears and rebuilds the gateway's watchedChunks set from scratch,
 * activates chunks in the activation radius, and builds snapshot messages
 * for newly-seen chunks.
 *
 * @param gwInfo            Gateway info (players, watchedChunks) - will be modified.
 * @param chunkRegistry     Chunk registry for accessing/activating chunks.
 * @param playerEntities    Map from PlayerId to entt::entity.
 * @param registry          The ECS registry.
 * @param tick              Current server tick.
 * @param watchRadius       Radius for watching chunks around players.
 * @param activationRadius  Radius for activating chunks around players.
 * @param generator         WorldGenerator for terrain generation (stateless).
 * @param acquireId         Optional callback to acquire a unique GlobalEntityId for entity spawning.
 * @return Vector of framed snapshot messages ready to send.
 */
inline std::vector<uint8_t> rebuildGatewayWatchedChunks(
    GatewayInfo& gwInfo,
    ChunkRegistry& chunkRegistry,
    const std::unordered_map<PlayerId, entt::entity>& playerEntities,
    entt::registry& registry,
    uint32_t tick,
    int32_t watchRadius,
    int32_t activationRadius,
    WorldGenerator& generator,
    std::function<GlobalEntityId()> acquireId = nullptr)
{
    std::vector<uint8_t> batchBuf;
    std::vector<ChunkId> activated; // Track locally for this call
    gwInfo.watchedChunks.clear();

    for (PlayerId pid : gwInfo.players) {
        auto entIt = playerEntities.find(pid);
        if (entIt == playerEntities.end()) continue;

        const auto& dyn = registry.get<DynamicPositionComponent>(entIt->second);
        const int32_t cx = dyn.x >> CHUNK_SHIFT_X;
        const int32_t cy = dyn.y >> CHUNK_SHIFT_Y;
        const int32_t cz = dyn.z >> CHUNK_SHIFT_Z;

        for (int32_t dx = -watchRadius; dx <= watchRadius; ++dx) {
            for (int32_t dy = -1; dy <= 1; ++dy) {
                for (int32_t dz = -watchRadius; dz <= watchRadius; ++dz) {
                    const ChunkId cid = ChunkId::make(cy + dy, cx + dx, cz + dz);
                    gwInfo.watchedChunks.insert(cid);

                    // Ensure activation-radius chunks exist; prepare snapshot on first sight
                    if (std::abs(dx) <= activationRadius && std::abs(dz) <= activationRadius) {
                        Chunk* chunk = chunkRegistry.activate(generator, cid, registry, tick, acquireId);
                        if (!gwInfo.lastStateTick.count(cid)) {
                            NetworkProtocol::appendFramed(batchBuf, chunk->buildSnapshot(registry, tick));
                            gwInfo.lastStateTick[cid] = tick;
                        }
                    }
                }
            }
        }
    }

    return batchBuf;
}

} // namespace ChunkMembershipSystem

} // namespace voxelmmo
