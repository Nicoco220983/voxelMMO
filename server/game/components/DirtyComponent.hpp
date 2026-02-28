#pragma once
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Tracks which components changed since the last snapshot or the last tick.
 *
 * Each bit corresponds to one component type (see POSITION_BIT in
 * DynamicPositionComponent.hpp). Typically one DirtyComponent is attached to every
 * entity that has serialisable state.
 *
 * - snapshotDirtyFlags: set by modify(), cleared after a snapshot delta is sent.
 * - tickDirtyFlags:     set by modify(), cleared at the end of every tick.
 */
struct DirtyComponent {
    uint8_t snapshotDirtyFlags{0};
    uint8_t tickDirtyFlags{0};

    /** @brief Mark a component as dirty at both snapshot and tick granularity. */
    void mark(uint8_t bit) noexcept {
        snapshotDirtyFlags |= bit;
        tickDirtyFlags     |= bit;
    }

    /** @brief Clear snapshot dirty flags (call after sending a snapshot delta). */
    void clearSnapshot() noexcept { snapshotDirtyFlags = 0; }

    /** @brief Clear tick dirty flags (call at the end of every tick). */
    void clearTick() noexcept { tickDirtyFlags = 0; }

    bool isSnapshotDirty() const noexcept { return snapshotDirtyFlags != 0; }
    bool isTickDirty()     const noexcept { return tickDirtyFlags     != 0; }
};

} // namespace voxelmmo
