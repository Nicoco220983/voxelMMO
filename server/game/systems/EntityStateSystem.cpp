#include "EntityStateSystem.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include <algorithm>

namespace voxelmmo {

namespace EntityStateSystem {

// Mark entity for creation
void markForCreation(entt::registry& registry, entt::entity ent, ChunkId chunkId) {
    registry.emplace<PendingCreateComponent>(ent, chunkId);
    registry.get<DirtyComponent>(ent).markCreated();
}

// Mark entity for deletion
void markForDeletion(entt::registry& registry, entt::entity ent) {
    // Avoid double-marking
    if (!registry.all_of<PendingDeleteComponent>(ent)) {
        registry.emplace<PendingDeleteComponent>(ent);
        registry.get<DirtyComponent>(ent).markDeleted();
    }
}

// Mark entity for chunk change
void markForChunkChange(entt::registry& registry, entt::entity ent, ChunkId newChunkId) {
    // Update or add PendingChunkChangeComponent
    if (registry.all_of<PendingChunkChangeComponent>(ent)) {
        registry.get<PendingChunkChangeComponent>(ent).newChunkId = newChunkId;
    } else {
        registry.emplace<PendingChunkChangeComponent>(ent, newChunkId);
    }
}

// Destroy all pending deletions after serialization
void destroyPendingDeletions(entt::registry& registry, std::vector<entt::entity>& entitiesToDestroy) {
    for (auto ent : entitiesToDestroy) {
        if (registry.valid(ent)) {
            registry.destroy(ent);
        }
    }
    entitiesToDestroy.clear();
}

// Main processing function
EntityStateResult apply(
    entt::registry& registry,
    ChunkRegistry& chunkRegistry,
    int32_t tickCount,
    WorldGenerator& generator)
{
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    EntityStateResult result;

    // ========================================================================
    // PHASE 1: Process chunk changes
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
    // PHASE 2: Process creations
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
    // PHASE 3: Collect deletions (do NOT destroy yet - serialization needs them)
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

} // namespace EntityStateSystem

} // namespace voxelmmo
