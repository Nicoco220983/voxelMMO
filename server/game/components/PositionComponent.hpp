#pragma once
#include "DirtyComponent.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

inline constexpr uint8_t POSITION_BIT = 1 << 0;

/**
 * @brief 3-D world-space position (float metres).
 */
struct PositionComponent {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    /**
     * @brief Set position and mark the owning entity dirty.
     * @param reg  ECS registry.
     * @param ent  Target entity (must have PositionComponent + DirtyComponent).
     * @param nx   New x coordinate.
     * @param ny   New y coordinate.
     * @param nz   New z coordinate.
     */
    static void modify(entt::registry& reg, entt::entity ent,
                       float nx, float ny, float nz) {
        reg.get<PositionComponent>(ent) = {nx, ny, nz};
        reg.get<DirtyComponent>(ent).mark(POSITION_BIT);
    }
};

} // namespace voxelmmo
