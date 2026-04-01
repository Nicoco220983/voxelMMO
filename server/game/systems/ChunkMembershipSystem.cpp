#include "game/systems/ChunkMembershipSystem.hpp"
#include "game/Chunk.hpp"
#include "game/WorldGenerator.hpp"
#include "game/entities/EntityFactory.hpp"
#include <iostream>

namespace voxelmmo {
namespace ChunkMembershipSystem {

void markForDeletion(entt::registry& registry, entt::entity ent) {
    if (!registry.all_of<PendingDeleteComponent>(ent)) {
        registry.emplace<PendingDeleteComponent>(ent);
    }
}

ChunkMembershipResult update(
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

size_t unloadUnwatchedChunks(
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
