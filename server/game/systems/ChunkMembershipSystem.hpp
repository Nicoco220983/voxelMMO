#pragma once
#include "game/Chunk.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/ChunkMembershipComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include "common/Types.hpp"
#include "common/GatewayInfo.hpp"
#include "common/NetworkProtocol.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>
#include <vector>
#include <set>

namespace voxelmmo {



// ── Result structures ─────────────────────────────────────────────────────────

/**
 * @brief Information about a player who entered a new chunk.
 *
 * The caller (GameEngine) uses this to send a SELF_ENTITY message
 * through the appropriate gateway.
 */
struct PlayerChunkEntry {
    PlayerId       playerId;
    ChunkId        chunkId;
    GlobalEntityId globalEntityId;
};

/**
 * @brief Result of ChunkMembershipSystem::updateEntities() containing all changes.
 */
struct ChunkMembershipSystemResult {
    /** Players who entered a new chunk and need SELF_ENTITY messages. */
    std::vector<PlayerChunkEntry> playerEntries;
    /** Chunks that were activated (created) during the update. */
    std::vector<ChunkId> activatedChunks;
};

// ── ChunkMembershipSystem ───────────────────────────────────────────────────────────────

/**
 * @brief Manages chunk membership for entities with DynamicPositionComponent.
 *
 * Phase A: Update per-chunk entity lists for moved entities (crossed chunk boundary).
 * Phase B: Rebuild gateway watchedChunks and activate newly-seen chunks.
 *
 * This system operates on the ECS registry and the chunk map, returning
 * results that the caller uses for gateway notifications.
 */
namespace ChunkMembershipSystem {

/**
 * @brief Helper to activate a chunk if it doesn't exist.
 * @return Reference to the chunk (either existing or newly created).
 */
inline Chunk& activateChunk(
    ChunkId cid,
    std::unordered_map<ChunkId, std::unique_ptr<Chunk>>& chunks,
    std::vector<ChunkId>& activatedOut)
{
    auto it = chunks.find(cid);
    if (it == chunks.end()) {
        auto chunk = std::make_unique<Chunk>(cid);
        chunk->world.generate(cid.x(), cid.y(), cid.z());
        Chunk* ptr = chunk.get();
        chunks[cid] = std::move(chunk);
        activatedOut.push_back(cid);
        return *ptr;
    }
    return *it->second;
}

/**
 * @brief Update chunk membership for all entities that have moved.
 *
 * Phase A: For entities with `moved=true`, compute new chunk from position,
 * remove from old chunk lists, add to new chunk (activating if needed).
 *
 * @param registry          The ECS registry.
 * @param chunks            Map of loaded chunks (may be modified: new chunks activated).
 * @param tickCount         Current server tick (for entity ID assignment tracking).
 * @param activationRadius  Radius for chunk activation around players.
 * @return Result containing player entries and activated chunks.
 */
inline ChunkMembershipSystemResult updateEntities(
    entt::registry& registry,
    std::unordered_map<ChunkId, std::unique_ptr<Chunk>>& chunks,
    int32_t tickCount,
    int32_t activationRadius)
{
    (void)tickCount; // Currently unused but kept for API consistency
    ChunkMembershipSystemResult result;

    auto view = registry.view<DynamicPositionComponent, ChunkMembershipComponent>();
    view.each([&](entt::entity ent, DynamicPositionComponent& dyn, ChunkMembershipComponent& cm) {
        if (!dyn.moved) return;
        dyn.moved = false;

        const int32_t cx = dyn.x >> CHUNK_SHIFT_X;
        const int32_t cy = dyn.y >> CHUNK_SHIFT_Y;
        const int32_t cz = dyn.z >> CHUNK_SHIFT_Z;
        const ChunkId newChunk = ChunkId::make(cy, cx, cz);

        if (newChunk == cm.currentChunkId) return;

        const bool     isPlayer = registry.all_of<PlayerComponent>(ent);
        const PlayerId pid      = isPlayer ? registry.get<PlayerComponent>(ent).playerId : 0u;

        // Remove from old chunk lists
        const int32_t ocx = cm.currentChunkId.x();
        const int32_t ocy = cm.currentChunkId.y();
        const int32_t ocz = cm.currentChunkId.z();

        // Get the global entity ID before we move the entity (for SELF_ENTITY message)
        const GlobalEntityId globalId = isPlayer 
            ? registry.get<GlobalEntityIdComponent>(ent).id 
            : GlobalEntityId{0};

        if (auto it = chunks.find(cm.currentChunkId); it != chunks.end()) {
            it->second->entities.erase(ent);
            if (isPlayer) it->second->presentPlayers.erase(pid);
        }
        if (isPlayer) {
            for (int32_t dx = -activationRadius; dx <= activationRadius; ++dx)
            for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dz = -activationRadius; dz <= activationRadius; ++dz) {
                const ChunkId cid = ChunkId::make(ocy + dy, ocx + dx, ocz + dz);
                if (auto it = chunks.find(cid); it != chunks.end())
                    it->second->watchingPlayers.erase(pid);
            }
        }

        // Ensure new chunk exists (activate if needed) and add to it
        Chunk& nc = activateChunk(newChunk, chunks, result.activatedChunks);
        nc.entities.insert(ent);

        if (isPlayer) {
            nc.presentPlayers.insert(pid);
            for (int32_t dx = -activationRadius; dx <= activationRadius; ++dx)
            for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dz = -activationRadius; dz <= activationRadius; ++dz) {
                const ChunkId cid = ChunkId::make(cy + dy, cx + dx, cz + dz);
                Chunk& watchChunk = activateChunk(cid, chunks, result.activatedChunks);
                watchChunk.watchingPlayers.insert(pid);
            }
            // Record player entry for SELF_ENTITY message (with stable global ID)
            result.playerEntries.push_back({
                pid,
                newChunk,
                globalId
            });
        }

        cm.currentChunkId = newChunk;
    });

    return result;
}

/**
 * @brief Rebuild watched chunks for a gateway based on its players' positions.
 *
 * Phase B: Clears and rebuilds the gateway's watchedChunks set from scratch,
 * activates chunks in the activation radius, and builds snapshot messages
 * for newly-seen chunks.
 *
 * @param gwInfo            Gateway info (players, watchedChunks) - will be modified.
 * @param chunks            Map of loaded chunks (may activate new chunks).
 * @param playerEntities    Map from PlayerId to entt::entity.
 * @param registry          The ECS registry.
 * @param tick              Current server tick.
 * @param watchRadius       Radius for watching chunks around players.
 * @param activationRadius  Radius for activating chunks around players.
 * @return Vector of framed snapshot messages ready to send.
 */
inline std::vector<uint8_t> rebuildGatewayWatchedChunks(
    GatewayInfo& gwInfo,
    std::unordered_map<ChunkId, std::unique_ptr<Chunk>>& chunks,
    const std::unordered_map<PlayerId, entt::entity>& playerEntities,
    entt::registry& registry,
    uint32_t tick,
    int32_t watchRadius,
    int32_t activationRadius)
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
                        Chunk& chunk = activateChunk(cid, chunks, activated);
                        if (!gwInfo.lastStateTick.count(cid)) {
                            NetworkProtocol::appendFramed(batchBuf, chunk.buildSnapshot(registry, tick));
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
