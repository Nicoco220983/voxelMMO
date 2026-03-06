#pragma once
#include "game/ChunkRegistry.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/PendingCreateComponent.hpp"
#include "game/components/PendingDeleteComponent.hpp"
#include "common/Types.hpp"
#include "common/GatewayInfo.hpp"
#include "common/NetworkProtocol.hpp"
#include <entt/entt.hpp>
#include <vector>
#include <set>
#include <optional>

namespace voxelmmo {

// Forward declarations
class WorldGenerator;
class EntityFactory;

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
 * @brief Result of detectChunkCrossings containing detected crossings.
 */
struct ChunkCrossingResult {
    /** Chunks that were activated (created) during the update. */
    std::vector<ChunkId> activatedChunks;
};

/**
 * @brief Result of rebuildGatewayWatchedChunks containing activated chunks.
 */
struct WatchedChunksResult {
    std::vector<uint8_t> snapshotBatch;
    std::vector<ChunkId> activatedChunks;  ///< Chunks that were activated this call
};

// ── Forward declarations ──────────────────────────────────────────────────────

namespace ChunkMembershipSystem {

/**
 * @brief Compute ChunkId from entity's position.
 */
inline ChunkId chunkIdFromPosition(const DynamicPositionComponent& dyn) {
    return ChunkId::make(
        dyn.y >> CHUNK_SHIFT_Y,
        dyn.x >> CHUNK_SHIFT_X,
        dyn.z >> CHUNK_SHIFT_Z
    );
}

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
 * and add to the old chunk's `movedEntities` set. The actual move
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

    // Find entities that have moved and mark them in their old chunk's movedEntities set
    auto view = registry.view<DynamicPositionComponent>();
    view.each([&](entt::entity ent, DynamicPositionComponent& dyn) {
        if (!dyn.moved) return;
        dyn.moved = false;

        const ChunkId newChunkId = chunkIdFromPosition(dyn);
        const int32_t cx = newChunkId.x();
        const int32_t cy = newChunkId.y();
        const int32_t cz = newChunkId.z();

        const bool isPlayer = registry.all_of<PlayerComponent>(ent);
        const PlayerId pid = isPlayer ? registry.get<PlayerComponent>(ent).playerId : 0u;

        // Find which chunk currently owns this entity (search in likely chunks)
        // For simplicity, we check chunks around the new position and old watching radius
        bool found = false;
        for (int32_t dx = -1; dx <= 1 && !found; ++dx)
        for (int32_t dy = -1; dy <= 1 && !found; ++dy)
        for (int32_t dz = -1; dz <= 1 && !found; ++dz) {
            const ChunkId cid = ChunkId::make(cy + dy, cx + dx, cz + dz);
            if (Chunk* chunk = chunkRegistry.getChunkMutable(cid)) {
                if (chunk->entities.count(ent)) {
                    // Entity is currently in this chunk
                    if (cid == newChunkId) {
                        // Still in same chunk, no change
                        found = true;
                        break;
                    }
                    
                    // Mark as moved
                    chunk->movedEntities.insert(ent);
                    found = true;
                    
                    // Remove from old chunk's watchingPlayers if player
                    if (isPlayer) {
                        const int32_t ocx = cid.x();
                        const int32_t ocy = cid.y();
                        const int32_t ocz = cid.z();
                        for (int32_t odx = -activationRadius; odx <= activationRadius; ++odx)
                        for (int32_t ody = -1; ody <= 1; ++ody)
                        for (int32_t odz = -activationRadius; odz <= activationRadius; ++odz) {
                            const ChunkId oldCid = ChunkId::make(ocy + ody, ocx + odx, ocz + odz);
                            if (Chunk* oldChunk = chunkRegistry.getChunkMutable(oldCid))
                                oldChunk->watchingPlayers.erase(pid);
                        }
                    }
                    break;
                }
            }
        }

        // For players, set up watching in new radius
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
 * Also sends SELF_ENTITY messages to gateways when player entities are created.
 *
 * This function DOES NOT generate entities for chunks - that is the caller's
 * responsibility via WorldGenerator::generateEntities().
 *
 * @param registry      The ECS registry.
 * @param chunkRegistry Chunk registry for accessing/activating chunks.
 * @param gateways      Gateway info map (for routing SELF_ENTITY messages).
 * @param tick          Current server tick (for SELF_ENTITY message).
 * @param outputCb      Callback to send SELF_ENTITY messages (can be null).
 * @return Statistics about processed entities + list of entities to destroy.
 */
inline EntityChunkResult updateEntitiesChunks(
    entt::registry& registry,
    ChunkRegistry& chunkRegistry,
    std::unordered_map<GatewayId, GatewayInfo>& gateways,
    uint32_t tick,
    std::function<void(GatewayId, const uint8_t*, size_t)>& outputCb)
{
    EntityChunkResult result;
    
    // Helper to send SELF_ENTITY message to the player's gateway
    auto sendSelfEntity = [&](PlayerId playerId, const ChunkId& chunkId, GlobalEntityId globalId) {
        if (!outputCb) return;
        for (auto& [gwId, gwInfo] : gateways) {
            if (gwInfo.players.count(playerId)) {
                const auto msg = NetworkProtocol::buildSelfEntityMessage(chunkId, tick, globalId);
                outputCb(gwId, msg.data(), msg.size());
                break;
            }
        }
    };

    // ========================================================================
    // Phase 1: Process chunk changes (from Chunk.movedEntities)
    // ========================================================================
    {
        for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
            for (entt::entity ent : chunkPtr->movedEntities) {
                if (!registry.valid(ent)) continue;
                
                auto* dyn = registry.try_get<DynamicPositionComponent>(ent);
                if (!dyn) continue;
                
                const ChunkId newChunkId = chunkIdFromPosition(*dyn);
                if (newChunkId == cid) {
                    // Still in same chunk, skip
                    continue;
                }

                // Remove from old chunk's entity set
                chunkPtr->entities.erase(ent);

                // Activate new chunk and add entity
                Chunk* newChunk = chunkRegistry.activate(newChunkId);
                if (!newChunk) {
                    // Chunk doesn't exist - skip this entity for now
                    continue;
                }
                newChunk->entities.insert(ent);

                // Mark as CREATED in new chunk (for viewers who haven't seen it)
                if (auto* dirty = registry.try_get<DirtyComponent>(ent)) {
                    dirty->markCreated();
                }

                ++result.chunkChanged;
            }
            // Clear movedEntities after processing
            chunkPtr->movedEntities.clear();
        }
    }

    // ========================================================================
    // Phase 2: Process creations
    // ========================================================================
    {
        auto view = registry.view<PendingCreateComponent, DynamicPositionComponent, DirtyComponent>();
        std::vector<entt::entity> processed;
        processed.reserve(view.size_hint());

        for (auto ent : view) {
            auto& pcc = view.get<PendingCreateComponent>(ent);
            auto& dyn = view.get<DynamicPositionComponent>(ent);

            // Use current position to determine chunk (may differ from target if entity moved)
            const ChunkId chunkId = chunkIdFromPosition(dyn);

            // Ensure chunk is activated and add entity
            Chunk* chunk = chunkRegistry.activate(chunkId);
            if (!chunk) {
                // Also try the target chunk from PendingCreateComponent
                Chunk* targetChunk = chunkRegistry.activate(pcc.targetChunkId);
                if (!targetChunk) {
                    // Chunk doesn't exist - skip
                    continue;
                }
                targetChunk->entities.insert(ent);
                
                // Add to present players if it's a player, and send SELF_ENTITY
                if (registry.all_of<PlayerComponent>(ent)) {
                    const auto& pc = registry.get<PlayerComponent>(ent);
                    targetChunk->presentPlayers.insert(pc.playerId);
                    
                    const auto& globalIdComp = registry.get<GlobalEntityIdComponent>(ent);
                    sendSelfEntity(pc.playerId, pcc.targetChunkId, globalIdComp.id);
                }
            } else {
                chunk->entities.insert(ent);

                // Add to present players if it's a player, and send SELF_ENTITY
                if (registry.all_of<PlayerComponent>(ent)) {
                    const auto& pc = registry.get<PlayerComponent>(ent);
                    chunk->presentPlayers.insert(pc.playerId);
                    
                    const auto& globalIdComp = registry.get<GlobalEntityIdComponent>(ent);
                    sendSelfEntity(pc.playerId, chunkId, globalIdComp.id);
                }
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
        auto view = registry.view<PendingDeleteComponent, DirtyComponent>();
        
        for (auto ent : view) {
            auto& dirty = view.get<DirtyComponent>(ent);

            // Find and remove from chunk's entity set
            // Search in chunks around the entity's last known position if available
            ChunkId lastChunkId;
            bool foundChunk = false;
            
            if (auto* dyn = registry.try_get<DynamicPositionComponent>(ent)) {
                lastChunkId = chunkIdFromPosition(*dyn);
                foundChunk = true;
            }
            
            if (foundChunk) {
                if (Chunk* chunk = chunkRegistry.getChunkMutable(lastChunkId)) {
                    chunk->entities.erase(ent);
                    
                    // Remove from present players if it's a player
                    if (registry.all_of<PlayerComponent>(ent)) {
                        const auto& pc = registry.get<PlayerComponent>(ent);
                        chunk->presentPlayers.erase(pc.playerId);
                    }
                }
                
                // Also check if entity is in movedEntities of any chunk
                for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
                    chunkPtr->movedEntities.erase(ent);
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
 * @brief Rebuild watched chunks for a gateway based on its players' positions.
 *
 * Phase C: Clears and rebuilds the gateway's watchedChunks set from scratch,
 * generates/activates chunks in the activation radius, and builds snapshot messages
 * for newly-seen chunks.
 *
 * Entity generation is the caller's responsibility - this function returns the list
 * of activated chunks so the caller can call WorldGenerator::generateEntities().
 *
 * @param gwInfo            Gateway info (players, watchedChunks) - will be modified.
 * @param chunkRegistry     Chunk registry for accessing/activating chunks.
 * @param playerEntities    Map from PlayerId to entt::entity.
 * @param registry          The ECS registry.
 * @param tick              Current server tick.
 * @param watchRadius       Radius for watching chunks around players.
 * @param activationRadius  Radius for activating chunks around players.
 * @param generator         WorldGenerator for terrain generation (stateless).
 * @return WatchedChunksResult containing snapshot batch and list of activated chunks.
 */
inline WatchedChunksResult rebuildGatewayWatchedChunks(
    GatewayInfo& gwInfo,
    ChunkRegistry& chunkRegistry,
    const std::unordered_map<PlayerId, entt::entity>& playerEntities,
    entt::registry& registry,
    uint32_t tick,
    int32_t watchRadius,
    int32_t activationRadius,
    WorldGenerator& generator)
{
    WatchedChunksResult result;
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
                        bool wasNew = !chunkRegistry.hasChunk(cid);
                        Chunk* chunk = chunkRegistry.generateAndActivate(generator, cid);
                        if (chunk && wasNew) {
                            result.activatedChunks.push_back(cid);
                        }
                        if (chunk && !gwInfo.lastStateTick.count(cid)) {
                            NetworkProtocol::appendFramed(result.snapshotBatch, chunk->buildSnapshot(registry, tick));
                            gwInfo.lastStateTick[cid] = tick;
                        }
                    }
                }
            }
        }
    }

    return result;
}

} // namespace ChunkMembershipSystem

} // namespace voxelmmo
