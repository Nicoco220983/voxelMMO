#pragma once
#include "game/ChunkRegistry.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/PendingDeleteComponent.hpp"
#include "game/components/ChunkMembershipComponent.hpp"
#include "common/Types.hpp"
#include "game/GatewayInfo.hpp"

// chunkIdOf is defined in common/Types.hpp
#include <entt/entt.hpp>
#include <vector>
#include <set>

namespace voxelmmo {

// Forward declarations
class WorldGenerator;
class EntityFactory;

// ── Result structures ─────────────────────────────────────────────────────────


/**
 * @brief Result of updatePlayersWatchedChunks containing activated chunks.
 */
struct WatchedChunksResult {
    std::vector<ChunkId> activatedChunks;  ///< Chunks that were activated this call
};

// ── Forward declarations ──────────────────────────────────────────────────────

namespace ChunkMembershipSystem {


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
        // DirtyComponent is not modified - PendingDeleteComponent is the source of truth
    }
}

/**
 * @brief Check and update entity chunk membership.
 *
 * For entities with `moved=true`, computes new chunk from position,
 * removes entity from old chunk.entities, adds to old chunk.leftEntities,
 * and updates chunk.presentPlayers for players.
 *
 * Uses ChunkMembershipComponent for O(1) lookup of current chunk (no searching).
 *
 * @param registry          The ECS registry.
 * @param chunkRegistry     Chunk registry for accessing chunks.
 */
inline void checkChunkMembership(
    entt::registry& registry,
    ChunkRegistry& chunkRegistry)
{
    // Find entities that have moved and update their chunk membership
    // ChunkMembershipComponent is mandatory - all entities must have it
    auto view = registry.view<DynamicPositionComponent, ChunkMembershipComponent>();
    view.each([&](entt::entity ent, DynamicPositionComponent& dyn, ChunkMembershipComponent& membership) {
        if (!dyn.moved) return;
        dyn.moved = false;

        const ChunkId newChunkId = ChunkId::fromSubVoxelPos(dyn.x, dyn.y, dyn.z);
        
        // No chunk change - skip
        if (membership.currentChunkId == newChunkId) return;

        const bool isPlayer = registry.all_of<PlayerComponent>(ent);
        PlayerId pid = 0;
        if (isPlayer) {
            pid = registry.get<PlayerComponent>(ent).playerId;
        }

        // Remove from old chunk
        if (Chunk* oldChunk = chunkRegistry.getChunkMutable(membership.currentChunkId)) {
            oldChunk->entities.erase(ent);
            oldChunk->leftEntities.insert(ent);
            if (isPlayer) {
                oldChunk->presentPlayers.erase(pid);
            }
        }

        // Add to new chunk
        if (Chunk* newChunk = chunkRegistry.getChunkMutable(newChunkId)) {
            newChunk->entities.insert(ent);
            if (isPlayer) {
                newChunk->presentPlayers.insert(pid);
            }
        }

        // Update component
        membership.currentChunkId = newChunkId;
    });
}

/**
 * @brief Update watched chunks for all players, and activate chunks if they do not exist.
 *
 * For each gateway:
 * - Clears and rebuilds the gateway's watchedChunks set
 * - Generates/activates chunks in the watch radius
 * - Updates Chunk.watchingPlayers for all affected chunks
 * - Generates entities for newly activated chunks
 *
 * This function does NOT send snapshots - caller is responsible for that.
 *
 * @param gateways          Map of gateway info (will be modified).
 * @param playerEntities    Map from PlayerId to entt::entity.
 * @param chunkRegistry     Chunk registry for accessing/activating chunks.
 * @param registry          The ECS registry.
 * @param watchRadius       Radius for watching chunks around players.
 * @param activationRadius  Radius for activating chunks around players.
 * @param generator         WorldGenerator for terrain and entity generation.
 * @param entityFactory     Factory to queue entity spawn requests.
 * @param tick              Current server tick.
 * @return WatchedChunksResult containing list of activated chunks.
 */
inline WatchedChunksResult updateAndActivatePlayersWatchedChunks(
    std::unordered_map<GatewayId, GatewayInfo>& gateways,
    const std::unordered_map<PlayerId, entt::entity>& playerEntities,
    ChunkRegistry& chunkRegistry,
    entt::registry& registry,
    int32_t watchRadius,
    int32_t activationRadius,
    WorldGenerator& generator,
    EntityFactory& entityFactory,
    uint32_t tick)
{
    WatchedChunksResult result;
    
    // First, clear all watchingPlayers (will be rebuilt)
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->watchingPlayers.clear();
    }

    // Rebuild watchingPlayers and gateway watchedChunks
    for (auto& [gwId, gwInfo] : gateways) {
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

                        // Ensure activation-radius chunks exist
                        if (std::abs(dx) <= activationRadius && std::abs(dz) <= activationRadius) {
                            bool wasNew = !chunkRegistry.hasChunk(cid);
                            Chunk* chunk = chunkRegistry.generateAndActivate(generator, cid, entityFactory, tick);
                            if (chunk && wasNew) {
                                result.activatedChunks.push_back(cid);
                            }
                        }
                        
                        // Update watchingPlayers
                        if (Chunk* chunk = chunkRegistry.getChunkMutable(cid)) {
                            chunk->watchingPlayers.insert(pid);
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
