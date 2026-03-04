#pragma once
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Tracks entity lifecycle events and component changes for serialization.
 *
 * Component dirty bits (0-5): defined in respective component headers
 *   - POSITION_BIT = 1 << 0 (DynamicPositionComponent.hpp)
 *   - SHEEP_BEHAVIOR_BIT = 1 << 1 (SheepBehaviorComponent.hpp)
 *
 * Lifecycle bits (6-7): defined here
 *   - CREATED_BIT: entity newly created this tick
 *   - DELETED_BIT: entity marked for deletion
 *
 * Granularity:
 *   - snapshotDirtyFlags: persists until snapshot delta is sent (every N ticks)
 *   - tickDirtyFlags: cleared after every tick delta
 */
struct DirtyComponent {
    uint8_t snapshotDirtyFlags{0};
    uint8_t tickDirtyFlags{0};

    // Entity lifecycle bits (high bits reserved for system use)
    static constexpr uint8_t CREATED_BIT = 1 << 6;  ///< Entity newly created
    static constexpr uint8_t DELETED_BIT = 1 << 7;  ///< Entity marked for deletion

    /** @brief Mark a component/lifecycle bit dirty at both snapshot and tick granularity. */
    void mark(uint8_t bit) noexcept {
        snapshotDirtyFlags |= bit;
        tickDirtyFlags     |= bit;
    }

    // Lifecycle helpers
    void markCreated() noexcept { mark(CREATED_BIT); }
    void markDeleted() noexcept { mark(DELETED_BIT); }

    bool isCreated() const noexcept { return (snapshotDirtyFlags & CREATED_BIT) != 0; }
    bool isDeleted() const noexcept { return (snapshotDirtyFlags & DELETED_BIT) != 0; }

    /** @brief Check if any component bits (excluding lifecycle) are dirty. */
    bool hasComponentChanges() const noexcept {
        return (snapshotDirtyFlags & 0x3F) != 0;  // Bits 0-5
    }

    /** @brief Clear snapshot dirty flags (call after sending a snapshot delta). */
    void clearSnapshot() noexcept { snapshotDirtyFlags = 0; }

    /** @brief Clear tick dirty flags (call at the end of every tick). */
    void clearTick() noexcept { tickDirtyFlags = 0; }

    bool isSnapshotDirty() const noexcept { return snapshotDirtyFlags != 0; }
    bool isTickDirty()     const noexcept { return tickDirtyFlags     != 0; }
};

} // namespace voxelmmo
