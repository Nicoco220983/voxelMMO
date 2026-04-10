#include "game/systems/HealthSystem.hpp"

namespace voxelmmo {
namespace HealthSystem {

void processDeathTimeouts(entt::registry& registry, uint32_t tick) {
    // Mark TTL-expired entities with DELETE_ENTITY delta type.
    // This runs BEFORE serialization so clients receive the DELETE delta.
    
    auto view = registry.view<HealthComponent, DirtyComponent>();
    for (auto ent : view) {
        const auto& health = view.get<HealthComponent>(ent);
        if (health.deleteAtTick > 0 && health.shouldDelete(tick)) {
            auto& dirty = view.get<DirtyComponent>(ent);
            dirty.markForDeletion();
        }
    }
}

} // namespace HealthSystem
} // namespace voxelmmo
