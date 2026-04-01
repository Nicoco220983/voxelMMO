#pragma once
#include "game/ChunkRegistry.hpp"
#include "game/SaveSystem.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/PendingDeleteComponent.hpp"
#include "game/components/ChunkMembershipComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "common/Types.hpp"
#include "game/GatewayInfo.hpp"

#include <entt/entt.hpp>
#include <vector>
#include <set>

namespace voxelmmo {

// Forward declarations
class WorldGenerator;
class EntityFactory;

/**
 * @brief Result of updateChunkMembership containing activated chunks.
 */
struct ChunkMembershipResult {
    std::vector<ChunkId> activatedChunks;  ///< Chunks that were activated this call
};

namespace ChunkMembershipSystem {

/**
 * @brief Mark an entity for deletion at end of tick.
 *
 * @param registry  The ECS registry.
 * @param ent       The entity to mark for deletion.
 */
inline void markForDeletion(entt::registry& registry, entt::entity ent) {
    if (!registry.all_of<PendingDeleteComponent>(ent)) {
        registry.emplace<PendingDeleteComponent>(ent);
    }
}

/**
 * @brief Update chunk membership for all entities and rebuild chunk player sets.
 *
 * This function performs a unified update:
 * 1. Clears and rebuilds chunk.entities and chunk.presentPlayers from living entities
 * 2. Handles entity movement between chunks
 * 3. Clears and rebuilds watchingPlayers from gateway player lists
 * 4. Activates chunks in player watch radius
 *
 * Using a rebuild pattern ensures consistency after entity deletions without
 * needing separate cleanup passes.
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
 * @return ChunkMembershipResult containing list of activated chunks.
 */
inline ChunkMembershipResult update(
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
    ChunkMembershipResult result;

    // Phase 1: Clear all chunk entity sets (will be rebuilt from living entities)
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->entities.clear();
        chunkPtr->presentPlayers.clear();
        chunkPtr->watchingPlayers.clear();
        chunkPtr->leftEntities.clear();
    }

    // Phase 2: Rebuild chunk.entities and presentPlayers from all living entities
    // Also handle entity movement between chunks
    auto entityView = registry.view<DynamicPositionComponent, ChunkMembershipComponent>();
    entityView.each([&](entt::entity ent, DynamicPositionComponent& dyn, ChunkMembershipComponent& membership) {
        const ChunkId newChunkId = ChunkId::fromSubVoxelPos(dyn.x, dyn.y, dyn.z);
        
        // Check if entity changed chunks
        if (membership.currentChunkId != newChunkId) {
            // Track in old chunk's leftEntities for delta serialization
            if (Chunk* oldChunk = chunkRegistry.getChunkMutable(membership.currentChunkId)) {
                oldChunk->leftEntities.insert(ent);
            }
            // Mark entity with CREATE_ENTITY delta type so new chunk serializes full state
            auto& dirty = registry.get<DirtyComponent>(ent);
            dirty.markCreated();
            
            // Update membership
            membership.currentChunkId = newChunkId;
            dyn.moved = false;
        }

        // Add to current chunk's entity set
        if (Chunk* chunk = chunkRegistry.getChunkMutable(newChunkId)) {
            chunk->entities.insert(ent);
            
            // If player, also add to presentPlayers
            if (auto* playerComp = registry.try_get<PlayerComponent>(ent)) {
                chunk->presentPlayers.insert(playerComp->playerId);
            }
        }
    });

    // Phase 3: Rebuild watchingPlayers and activate chunks near players
    // Note: All chunks are now broadcast to all gateways, so we don't track per-gateway watched chunks.
    // We still activate chunks near players and track watchingPlayers for other purposes.
    for (auto& [gwId, gwInfo] : gateways) {
        for (PlayerId pid : gwInfo.players) {
            auto entIt = playerEntities.find(pid);
            if (entIt == playerEntities.end()) continue;

            const auto& dyn = registry.get<DynamicPositionComponent>(entIt->second);
            const ChunkPos cpos = subVoxelToChunkPos(dyn.x, dyn.y, dyn.z);

            for (int32_t dx = -watchRadius; dx <= watchRadius; ++dx) {
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dz = -watchRadius; dz <= watchRadius; ++dz) {
                        const ChunkId cid = ChunkId::make(cpos.y + dy, cpos.x + dx, cpos.z + dz);

                        // Ensure activation-radius chunks exist
                        if (std::abs(dx) <= activationRadius && std::abs(dz) <= activationRadius) {
                            bool wasNew = !chunkRegistry.hasChunk(cid);
                            Chunk* chunk = chunkRegistry.generate(generator, cid);
                            chunkRegistry.activate(cid, generator, entityFactory, tick);
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

/**
 * @brief Unload chunks that are no longer watched by any player.
 *
 * This function should be called after update() has rebuilt watchingPlayers.
 * Chunks with empty watchingPlayers are saved and unloaded from memory.
 *
 * @param chunkRegistry     Chunk registry for accessing/unloading chunks.
 * @param registry          The ECS registry (for entity cleanup).
 * @param saveSystem        SaveSystem for persisting chunks before unload.
 * @return Number of chunks unloaded.
 */
inline size_t unloadUnwatchedChunks(
    ChunkRegistry& chunkRegistry,
    entt::registry& registry,
    SaveSystem* saveSystem)
{
    size_t unloadedCount = 0;
    
    // Collect chunks to unload (can't modify while iterating)
    std::vector<ChunkId> toUnload;
    
    for (const auto& [cid, chunkPtr] : chunkRegistry.getAllChunks()) {
        // Unload if:
        // 1. No players watching
        // 2. Not activated (entities already removed)
        // 3. Has been saved before or we can save it now
        if (chunkPtr->watchingPlayers.empty() && !chunkPtr->activated) {
            toUnload.push_back(cid);
        }
    }
    
    // Save and unload
    for (const ChunkId& cid : toUnload) {
        if (saveSystem) {
            if (const Chunk* chunk = chunkRegistry.getChunk(cid)) {
                saveSystem->saveChunkVoxels(cid, chunk->world.voxels);
            }
        }
        
        if (chunkRegistry.unload(cid)) {
            ++unloadedCount;
            auto pos = getChunkPos(cid);
            std::cout << "[ChunkMembership] Unloaded unwatched chunk ("
                      << pos.x << "," << pos.y << "," << pos.z << ")\n";
        }
    }
    
    if (unloadedCount > 0) {
        std::cout << "[ChunkMembership] Unloaded " << unloadedCount << " unwatched chunks\n";
    }
    
    return unloadedCount;
}

} // namespace ChunkMembershipSystem

} // namespace voxelmmo
