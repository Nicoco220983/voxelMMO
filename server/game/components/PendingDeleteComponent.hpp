#pragma once
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Marks an entity for deferred deletion with optional TTL.
 *
 * Set by ChunkMembershipSystem::markForDeletion() when an entity should be removed,
 * or by HealthComponent::applyDamage() when an entity dies.
 *
 * Entities marked with this component are NOT destroyed immediately - they remain
 * in the registry so their DELETE delta can be serialized to watching clients.
 * Actual destruction happens after TTL expires and serialization is complete.
 */
struct PendingDeleteComponent {
    uint32_t deleteAtTick{0};  ///< Tick when entity should be deleted (0 = immediate)
    
    explicit PendingDeleteComponent(uint32_t tick = 0) : deleteAtTick(tick) {}
};

inline constexpr uint32_t DEATH_DELETION_DELAY_TICKS = 180;  // ~3 seconds at 60tps

} // namespace voxelmmo
