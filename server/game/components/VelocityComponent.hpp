#pragma once
#include "DirtyComponent.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

inline constexpr uint8_t VELOCITY_BIT = 1 << 1;

/**
 * @brief 3-D velocity in metres per second.
 */
struct VelocityComponent {
    float vx{0.0f};
    float vy{0.0f};
    float vz{0.0f};

    /**
     * @brief Set velocity and mark the owning entity dirty.
     * @param reg  ECS registry.
     * @param ent  Target entity (must have VelocityComponent + DirtyComponent).
     * @param nvx  New x velocity.
     * @param nvy  New y velocity.
     * @param nvz  New z velocity.
     */
    static void modify(entt::registry& reg, entt::entity ent,
                       float nvx, float nvy, float nvz) {
        reg.get<VelocityComponent>(ent) = {nvx, nvy, nvz};
        reg.get<DirtyComponent>(ent).mark(VELOCITY_BIT);
    }
};

} // namespace voxelmmo
