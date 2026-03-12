#pragma once
#include <cstdint>
#include "common/MessageTypes.hpp"

namespace voxelmmo {

/**
 * @brief Tracks entity lifecycle events and component changes for serialization.
 *
 * Component dirty bits (0-5): defined in respective component headers
 *   - POSITION_BIT = 1 << 0 (DynamicPositionComponent.hpp)
 *   - SHEEP_BEHAVIOR_BIT = 1 << 1 (SheepBehaviorComponent.hpp)
 *
 * Delta types: Each entity record in a delta message has a DeltaType:
 *   - CREATE_ENTITY (0): entity newly spawned or moved from elsewhere
 *   - UPDATE_ENTITY (1): entity already known, only dirty components sent
 *   - DELETE_ENTITY (2): entity removed (tracked via PendingDeleteComponent)
 *   - CHUNK_CHANGE_ENTITY (3): entity moved to different chunk
 *
 * Granularity:
 *   - snapshotDeltaType / snapshotDirtyFlags: persists until snapshot delta is sent (every N ticks)
 *   - tickDeltaType / tickDirtyFlags: cleared after every tick delta
 */
struct DirtyComponent {
    uint8_t snapshotDirtyFlags{0};
    uint8_t tickDirtyFlags{0};
    DeltaType snapshotDeltaType{DeltaType::UPDATE_ENTITY};
    DeltaType tickDeltaType{DeltaType::UPDATE_ENTITY};

    /** @brief Mark a component dirty at both snapshot and tick granularity. */
    void mark(uint8_t bit) noexcept {
        snapshotDirtyFlags |= bit;
        tickDirtyFlags     |= bit;
    }

    // Delta type helpers
    void markCreated() noexcept {
        snapshotDeltaType = DeltaType::CREATE_ENTITY;
        tickDeltaType = DeltaType::CREATE_ENTITY;
    }

    bool isCreated() const noexcept {
        return snapshotDeltaType == DeltaType::CREATE_ENTITY;
    }

    /** @brief Check if any component bits (excluding lifecycle) are dirty. */
    bool hasComponentChanges() const noexcept {
        return (snapshotDirtyFlags & 0x3F) != 0;  // Bits 0-5
    }

    /** @brief Clear snapshot dirty flags (call after sending a snapshot delta). */
    void clearSnapshot() noexcept {
        snapshotDirtyFlags = 0;
        snapshotDeltaType = DeltaType::UPDATE_ENTITY;
    }

    /** @brief Clear tick dirty flags (call at the end of every tick). */
    void clearTick() noexcept {
        tickDirtyFlags = 0;
        tickDeltaType = DeltaType::UPDATE_ENTITY;
    }

    bool isSnapshotDirty() const noexcept { return snapshotDirtyFlags != 0 || snapshotDeltaType != DeltaType::UPDATE_ENTITY; }
    bool isTickDirty()     const noexcept { return tickDirtyFlags != 0 || tickDeltaType != DeltaType::UPDATE_ENTITY; }
};

} // namespace voxelmmo
