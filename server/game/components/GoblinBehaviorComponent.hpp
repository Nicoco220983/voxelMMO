#pragma once
#include "common/SafeBufWriter.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/SheepBehaviorComponent.hpp"  // For AI_BEHAVIOR_BIT
#include <entt/entt.hpp>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Goblin AI state component.
 *
 * State machine:
 *   - IDLE/WALKING: Wandering randomly (like sheep)
 *   - CHASE: Pursuing a target player
 *   - ATTACK: Performing melee attack
 *
 * Transitions:
 *   - Player in aggro radius (10 voxels) → CHASE
 *   - In attack range (2 voxels) → ATTACK
 *   - Target lost for 5+ seconds → return to IDLE
 */
struct GoblinBehaviorComponent {
    enum class State : uint8_t { IDLE = 0, WALKING = 1, CHASE = 2, ATTACK = 3 };

    State state{State::IDLE};
    uint32_t stateEndTick{0};        ///< Tick when to transition to next state
    int32_t targetX{0}, targetZ{0};  ///< Target position when walking/chasing (sub-voxels)
    float yaw{0};                    ///< Current rotation (radians, for facing movement)
    uint32_t targetEntityId{0};      ///< Target entity GlobalEntityId for CHASE/ATTACK
    uint32_t aggroCooldownTick{0};   ///< When to drop aggro and return to IDLE
    uint32_t attackCooldownTick{0};  ///< When next attack is available

    /**
     * @brief Check if any field differs from default values.
     * Used by serializeCreate to determine if component needs to be sent.
     */
    bool isNonDefault() const noexcept {
        return state != State::IDLE ||
               stateEndTick != 0 ||
               targetX != 0 ||
               targetZ != 0 ||
               yaw != 0.0f ||
               targetEntityId != 0 ||
               aggroCooldownTick != 0 ||
               attackCooldownTick != 0;
    }

    /**
     * @brief Serialize behavior state.
     * Wire layout: uint8 state
     */
    void serializeFields(SafeBufWriter& w) const noexcept {
        w.write(static_cast<uint8_t>(state));
    }

    /**
     * @brief Modify state and mark dirty (uses shared AI_BEHAVIOR_BIT).
     */
    static void modify(entt::registry& reg, entt::entity ent,
                       State newState, uint32_t endTick,
                       int32_t tx, int32_t tz, float newYaw, bool dirty) {
        auto& c = reg.get<GoblinBehaviorComponent>(ent);
        c.state = newState;
        c.stateEndTick = endTick;
        c.targetX = tx;
        c.targetZ = tz;
        c.yaw = newYaw;
        if (dirty) reg.get<DirtyComponent>(ent).mark(AI_BEHAVIOR_BIT);
    }

    /**
     * @brief Modify state with target and mark dirty (uses shared AI_BEHAVIOR_BIT).
     */
    static void modifyWithTarget(entt::registry& reg, entt::entity ent,
                                  State newState, uint32_t endTick,
                                  int32_t tx, int32_t tz, float newYaw,
                                  uint32_t targetId, uint32_t aggroTick, bool dirty) {
        auto& c = reg.get<GoblinBehaviorComponent>(ent);
        c.state = newState;
        c.stateEndTick = endTick;
        c.targetX = tx;
        c.targetZ = tz;
        c.yaw = newYaw;
        c.targetEntityId = targetId;
        c.aggroCooldownTick = aggroTick;
        if (dirty) reg.get<DirtyComponent>(ent).mark(AI_BEHAVIOR_BIT);
    }

    /**
     * @brief Modify attack cooldown without changing state.
     */
    static void setAttackCooldown(entt::registry& reg, entt::entity ent, uint32_t cooldownTick, bool dirty) {
        auto& c = reg.get<GoblinBehaviorComponent>(ent);
        c.attackCooldownTick = cooldownTick;
        if (dirty) reg.get<DirtyComponent>(ent).mark(AI_BEHAVIOR_BIT);
    }
};

} // namespace voxelmmo
