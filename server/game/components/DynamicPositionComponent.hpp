#pragma once
#include "DirtyComponent.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

inline constexpr uint8_t POSITION_BIT = 1 << 0;

/**
 * @brief 3-D position + velocity + physics flags in one serialisable component.
 *
 * The server keeps x/y/z always up-to-date: stepPhysics() advances every
 * entity by one tick each game tick, writing via modify(dirty=false).
 * Only state changes that clients need to know about (landing, new input)
 * use modify(dirty=true), which marks the component dirty and triggers a delta.
 *
 * Serialised layout (when POSITION_BIT is set):
 *   int32 x,y,z · int32 vx,vy,vz · uint8 grounded
 * The reference tick is NOT serialised here — it is embedded in the chunk
 * message header and set on the client from there.
 */
struct DynamicPositionComponent {
    int32_t x{0},  y{0},  z{0};   ///< World-space position (sub-voxels), always current.
    int32_t vx{0}, vy{0}, vz{0};  ///< Velocity (sub-voxels per tick).
    bool grounded{false};          ///< When false, GRAVITY_DECREMENT applies along -Y per tick.

    /**
     * @brief Overwrite all fields.
     * @param dirty  When true, marks POSITION_BIT dirty so a delta is sent to clients.
     *               Pass false for routine per-tick position advances.
     */
    static void modify(entt::registry& reg, entt::entity ent,
                       int32_t nx,  int32_t ny,  int32_t nz,
                       int32_t nvx, int32_t nvy, int32_t nvz,
                       bool    ngrounded, bool dirty) {
        reg.get<DynamicPositionComponent>(ent) = {nx, ny, nz, nvx, nvy, nvz, ngrounded};
        if (dirty) reg.get<DirtyComponent>(ent).mark(POSITION_BIT);
    }
};

} // namespace voxelmmo
