#pragma once
#include "common/SafeBufWriter.hpp"
#include <cstdint>

namespace voxelmmo {

/** @brief Bit for DirtyComponent to track sheep behavior changes. */
inline constexpr uint8_t SHEEP_BEHAVIOR_BIT = 1 << 1;

/**
 * @brief Sheep AI state component.
 *
 * Simple state machine: IDLE (2-5s) → WALKING (2s) → IDLE ...
 * Target position is set when entering WALKING state; sheep moves toward it.
 */
struct SheepBehaviorComponent {
    enum class State : uint8_t { IDLE = 0, WALKING = 1 };

    State state{State::IDLE};
    uint32_t stateEndTick{0};    ///< Tick when to transition to next state
    int32_t targetX{0}, targetZ{0};  ///< Target position when walking (sub-voxels)
    float yaw{0};                ///< Current rotation (radians, for facing movement)

    /**
     * @brief Serialize behavior state.
     * Wire layout: uint8 state
     */
    void serializeFields(SafeBufWriter& w) const noexcept {
        w.write(static_cast<uint8_t>(state));
    }

    /**
     * @brief Modify state and mark dirty.
     */
    static void modify(entt::registry& reg, entt::entity ent,
                       State newState, uint32_t endTick,
                       int32_t tx, int32_t tz, float newYaw, bool dirty) {
        auto& c = reg.get<SheepBehaviorComponent>(ent);
        c.state = newState;
        c.stateEndTick = endTick;
        c.targetX = tx;
        c.targetZ = tz;
        c.yaw = newYaw;
        if (dirty) reg.get<DirtyComponent>(ent).mark(SHEEP_BEHAVIOR_BIT);
    }
};

} // namespace voxelmmo
