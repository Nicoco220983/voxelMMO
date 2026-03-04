#pragma once
#include "game/Chunk.hpp"
#include "game/systems/EntityStateSystem.hpp"
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
 * @brief Result of ChunkMembershipSystem::updateEntities() containing all changes.
 */
struct ChunkMembershipSystemResult {
    /** Chunks that were activated (created) during the update. */
    std::vector<ChunkId> activatedChunks;
};

// ── ChunkMembershipSystem ───────────────────────────────────────────────────────────────

/**
 * @brief Manages chunk membership marking for entities with DynamicPositionComponent.
 *
 * Phase A: For entities with `moved=true` crossing chunk boundary, mark for chunk change.
 *          Actual moves are deferred to EntityStateSystem::apply().
 *
 * Phase B: Rebuild gateway watchedChunks and activate newly-seen chunks.
 *
 * This system operates on the ECS registry, returning results that the caller
 * uses for gateway notifications. The actual chunk moves happen in EntityStateSystem.
 */
namespace ChunkMembershipSystem {

/**
 * @brief Mark entities for chunk change when they cross chunk boundaries.
 *
 * Phase A: For entities with `moved=true`, compute new chunk from position,
 * and mark for chunk change via EntityStateSystem. The actual move happens
 * later in EntityStateSystem::apply().
 *
 * @param registry          The ECS registry.
 * @param chunks            Map of loaded chunks (passed through to EntityStateSystem).
 * @param tickCount         Current server tick.
 * @param activationRadius  Radius for chunk activation around players.
 * @return Result containing activated chunks (from CHUNK_CHANGE activations).
 */
inline ChunkMembershipSystemResult updateEntities(
    entt::registry& registry,
    std::unordered_map<ChunkId, std::unique_ptr<Chunk>>& chunks,
    int32_t tickCount,
    int32_t activationRadius)
{
    (void)tickCount;
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
                if (auto it = chunks.find(cid); it != chunks.end())
                    it->second->watchingPlayers.erase(pid);
            }
        }

        // Mark for chunk change - actual move happens in EntityStateSystem::apply()
        EntityStateSystem::markForChunkChange(registry, ent, newChunk);

        // For players, set up watching in new radius (chunk will be activated in Phase B)
        if (isPlayer) {
            for (int32_t dx = -activationRadius; dx <= activationRadius; ++dx)
            for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dz = -activationRadius; dz <= activationRadius; ++dz) {
                const ChunkId cid = ChunkId::make(cy + dy, cx + dx, cz + dz);
                if (auto it = chunks.find(cid); it != chunks.end()) {
                    it->second->watchingPlayers.insert(pid);
                }
            }
        }
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
                        Chunk& chunk = EntityStateSystem::activateChunk(cid, chunks, activated);
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
