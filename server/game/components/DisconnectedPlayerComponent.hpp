#pragma once
#include "common/Types.hpp"
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Marks a player entity for delayed deletion after disconnect.
 *
 * When a player disconnects, this component is added to their entity with
 * the current tick. The DisconnectedPlayerSystem will check these entities
 * and delete them after DISCONNECT_GRACE_PERIOD_TICKS if the player hasn't
 * reconnected.
 *
 * If the player reconnects before the grace period expires, this component
 * should be removed to keep the entity alive.
 */
struct DisconnectedPlayerComponent {
    /** Tick at which the player disconnected. */
    uint32_t disconnectTick;

    /** Grace period in ticks before entity deletion (60 seconds). */
    static constexpr uint32_t DISCONNECT_GRACE_PERIOD_TICKS = 60 * TICK_RATE;  // 1200 ticks at 20 tps

    explicit DisconnectedPlayerComponent(uint32_t tick) : disconnectTick(tick) {}

    /**
     * @brief Check if the grace period has expired.
     * @param currentTick The current server tick.
     * @return true if the entity should be deleted.
     */
    bool hasExpired(uint32_t currentTick) const noexcept {
        // Handle tick overflow (unlikely but safe)
        if (currentTick < disconnectTick) {
            return (UINT32_MAX - disconnectTick + currentTick) >= DISCONNECT_GRACE_PERIOD_TICKS;
        }
        return (currentTick - disconnectTick) >= DISCONNECT_GRACE_PERIOD_TICKS;
    }
};

} // namespace voxelmmo
