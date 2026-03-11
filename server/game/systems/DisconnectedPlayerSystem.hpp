#pragma once
#include "game/components/DisconnectedPlayerComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>

namespace voxelmmo {

/**
 * @brief System for handling disconnected player entities.
 *
 * When players disconnect, their entities are kept alive for a grace period
 * (DISCONNECT_GRACE_PERIOD_TICKS) to allow for reconnection. This system:
 * - Checks all entities with DisconnectedPlayerComponent
 * - Destroys expired player entities
 * - Removes them from the playerEntities map
 *
 * Chunk cleanup (presentPlayers, entities, watchingPlayers) is handled by
 * ChunkMembershipSystem::cleanupChunkEntitySets() which should be called
 * after this system.
 *
 * Called once per second by GameEngine::tick().
 */
namespace DisconnectedPlayerSystem {

/**
 * @brief Process disconnected player entities.
 *
 * Iterates over all entities with DisconnectedPlayerComponent and destroys
 * those whose grace period has expired.
 *
 * @param registry       The ECS registry.
 * @param playerEntities Map from PlayerId to entity (will be updated for deleted players).
 * @param currentTick    The current server tick.
 * @return Number of players deleted.
 */
inline size_t process(
    entt::registry& registry,
    std::unordered_map<PlayerId, entt::entity>& playerEntities,
    uint32_t currentTick)
{
    size_t deletedCount = 0;

    auto view = registry.view<DisconnectedPlayerComponent, PlayerComponent>();
    std::vector<entt::entity> toDelete;

    // Find all expired disconnections
    for (auto ent : view) {
        const auto& disc = view.get<DisconnectedPlayerComponent>(ent);
        if (disc.hasExpired(currentTick)) {
            toDelete.push_back(ent);
        }
    }

    // Process deletions - destroy entity and remove from playerEntities map
    for (auto ent : toDelete) {
        const auto& player = registry.get<PlayerComponent>(ent);
        playerEntities.erase(player.playerId);
        registry.destroy(ent);
        ++deletedCount;
    }

    return deletedCount;
}

/**
 * @brief Cancel a pending disconnection (player reconnected).
 *
 * Removes the DisconnectedPlayerComponent from the entity to keep it alive.
 *
 * @param registry The ECS registry.
 * @param ent      The player entity that reconnected.
 * @return true if the component was removed, false if not found.
 */
inline bool cancelDisconnection(entt::registry& registry, entt::entity ent) {
    if (registry.all_of<DisconnectedPlayerComponent>(ent)) {
        registry.remove<DisconnectedPlayerComponent>(ent);
        return true;
    }
    return false;
}

} // namespace DisconnectedPlayerSystem

} // namespace voxelmmo
