#include "game/systems/DisconnectedPlayerSystem.hpp"

namespace voxelmmo {
namespace DisconnectedPlayerSystem {

size_t process(
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

    // TODO: only add PendingDeletionComponent
    // Process deletions - destroy entity and remove from playerEntities map
    for (auto ent : toDelete) {
        const auto& player = registry.get<PlayerComponent>(ent);
        playerEntities.erase(player.playerId);
        registry.destroy(ent);
        ++deletedCount;
    }

    return deletedCount;
}

bool cancelDisconnection(entt::registry& registry, entt::entity ent) {
    if (registry.all_of<DisconnectedPlayerComponent>(ent)) {
        registry.remove<DisconnectedPlayerComponent>(ent);
        return true;
    }
    return false;
}

} // namespace DisconnectedPlayerSystem
} // namespace voxelmmo
