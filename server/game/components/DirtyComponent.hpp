#pragma once
#include <cstdint>
#include "common/NetworkProtocol.hpp"

namespace voxelmmo {

/**
 * @brief Tracks entity lifecycle events and component changes for serialization.
 *
 * Component dirty bits (0-5): defined in respective component headers
 *   - POSITION_BIT = 1 << 0 (DynamicPositionComponent.hpp)
 *   - SHEEP_BEHAVIOR_BIT = 1 << 1 (SheepBehaviorComponent.hpp)
 *   - HEALTH_BIT = 1 << 2 (HealthComponent.hpp)
 *
 * Delta types: Each entity record in a delta message has a DeltaType:
 *   - CREATE_ENTITY (0): entity newly spawned or moved from elsewhere
 *   - UPDATE_ENTITY (1): entity already known, only dirty components sent
 *   - DELETE_ENTITY (2): entity being removed (set by markForDeletion())
 *   - CHUNK_CHANGE_ENTITY (3): entity moved to different chunk
 *
 * Granularity:
 *   - deltaType / dirtyFlags: cleared after every tick delta
 */
struct DirtyComponent {
    uint8_t dirtyFlags{0};
    DeltaType deltaType{DeltaType::UPDATE_ENTITY};

    /** @brief Mark a component dirty. */
    void mark(uint8_t bit) noexcept {
        dirtyFlags |= bit;
    }

    // Delta type helpers
    void markCreated() noexcept {
        deltaType = DeltaType::CREATE_ENTITY;
    }

    bool isCreated() const noexcept {
        return deltaType == DeltaType::CREATE_ENTITY;
    }

    void markForDeletion() noexcept {
        deltaType = DeltaType::DELETE_ENTITY;
    }

    bool isDeleted() const noexcept {
        return deltaType == DeltaType::DELETE_ENTITY;
    }

    /** @brief Check if any component bits (excluding lifecycle) are dirty. */
    bool hasComponentChanges() const noexcept {
        return (dirtyFlags & 0x3F) != 0;  // Bits 0-5
    }

    /** @brief Clear dirty flags (call at the end of every tick). */
    void clear() noexcept {
        dirtyFlags = 0;
        deltaType = DeltaType::UPDATE_ENTITY;
    }

    bool isDirty() const noexcept { return dirtyFlags != 0 || deltaType != DeltaType::UPDATE_ENTITY; }
};

} // namespace voxelmmo
