#pragma once
#include "game/ChunkRegistry.hpp"
#include "game/SaveSystem.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/ChunkMembershipComponent.hpp"
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
void markForDeletion(entt::registry& registry, entt::entity ent);

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
 * @param saveSystem        SaveSystem to load saved chunks from.
 * @return ChunkMembershipResult containing list of activated chunks.
 */
ChunkMembershipResult update(
    std::unordered_map<GatewayId, GatewayInfo>& gateways,
    const std::unordered_map<PlayerId, entt::entity>& playerEntities,
    ChunkRegistry& chunkRegistry,
    entt::registry& registry,
    int32_t watchRadius,
    int32_t activationRadius,
    WorldGenerator& generator,
    EntityFactory& entityFactory,
    uint32_t tick,
    SaveSystem* saveSystem);

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
size_t unloadUnwatchedChunks(
    ChunkRegistry& chunkRegistry,
    entt::registry& registry,
    SaveSystem* saveSystem);

} // namespace ChunkMembershipSystem

} // namespace voxelmmo
