#include "game/systems/DisconnectedPlayerSystem.hpp"
#include "game/components/DirtyComponent.hpp"

namespace voxelmmo {
namespace DisconnectedPlayerSystem {

size_t process(
    entt::registry& registry,
    std::unordered_map<PlayerId, entt::entity>& playerEntities,
    uint32_t currentTick)
{
    size_t markedCount = 0;

    auto view = registry.view<DisconnectedPlayerComponent, PlayerComponent, DirtyComponent>();

    // Mark expired disconnection entities for deletion
    for (auto ent : view) {
        const auto& disc = view.get<DisconnectedPlayerComponent>(ent);
        if (disc.hasExpired(currentTick)) {
            // Mark for deletion - processPendingDeletions will handle actual cleanup
            view.get<DirtyComponent>(ent).markForDeletion();
            ++markedCount;
        }
    }

    return markedCount;
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
