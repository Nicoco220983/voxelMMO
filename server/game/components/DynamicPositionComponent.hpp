#pragma once
#include "DirtyComponent.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>
#include <tuple>

namespace voxelmmo {

inline constexpr uint8_t POSITION_BIT = 1 << 0;

/**
 * @brief 3-D position + velocity + physics flags in one serialisable component.
 *
 * Both sides (server and client) derive future position from the same
 * closed-form kinematic equations, so the server only needs to call modify()
 * when the prediction model actually changes (landing, jumping, player input):
 *
 *   dt  = (targetTick - tick) * TICK_DT
 *   x'  = x + vx * dt
 *   y'  = y + vy * dt  −  (grounded ? 0 : ½ * GRAVITY * dt²)
 *   z'  = z + vz * dt
 *   vy' = vy  −  (grounded ? 0 : GRAVITY * dt)
 *
 * Serialised layout (when POSITION_BIT is set):
 *   float x,y,z (advanced to message tick) · float vx,vy,vz · uint8 grounded
 * The reference tick is NOT serialised here — it is embedded in the chunk
 * message header and set on the client from there.
 */
struct DynamicPositionComponent {
    uint32_t tick{0};            ///< Reference game-tick when this state was captured.
    float x{0},  y{0},  z{0};   ///< World-space position (metres).
    float vx{0}, vy{0}, vz{0};  ///< Velocity (m/s).
    bool grounded{false};        ///< When false, GRAVITY applies along -Y.

    /**
     * @brief Overwrite all fields and mark the entity dirty.
     */
    static void modify(entt::registry& reg, entt::entity ent,
                       uint32_t ntick,
                       float nx,  float ny,  float nz,
                       float nvx, float nvy, float nvz,
                       bool  ngrounded) {
        reg.get<DynamicPositionComponent>(ent) =
            {ntick, nx, ny, nz, nvx, nvy, nvz, ngrounded};
        reg.get<DirtyComponent>(ent).mark(POSITION_BIT);
    }

    /** @brief Predicted (x, y, z) at targetTick using closed-form kinematics. */
    static std::tuple<float, float, float> predictAt(
        const DynamicPositionComponent& s, uint32_t targetTick)
    {
        const float dt = static_cast<float>(targetTick - s.tick) * TICK_DT;
        const float py = s.y + s.vy * dt
                       - (s.grounded ? 0.0f : 0.5f * GRAVITY * dt * dt);
        return {s.x + s.vx * dt, py, s.z + s.vz * dt};
    }

    /** @brief Predicted vy at targetTick. */
    static float predictVy(const DynamicPositionComponent& s, uint32_t targetTick)
    {
        if (s.grounded) return s.vy;
        const float dt = static_cast<float>(targetTick - s.tick) * TICK_DT;
        return s.vy - GRAVITY * dt;
    }
};

} // namespace voxelmmo
