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
 *   float x,y,z · float vx,vy,vz · uint8 grounded
 * The reference tick is NOT serialised here — it is embedded in the chunk
 * message header and set on the client from there.
 */
struct DynamicPositionComponent {
    float x{0},  y{0},  z{0};   ///< World-space position (metres), always current.
    float vx{0}, vy{0}, vz{0};  ///< Velocity (m/s).
    bool grounded{false};        ///< When false, GRAVITY applies along -Y.

    /**
     * @brief Overwrite all fields.
     * @param dirty  When true, marks POSITION_BIT dirty so a delta is sent to clients.
     *               Pass false for routine per-tick position advances.
     */
    static void modify(entt::registry& reg, entt::entity ent,
                       float nx,  float ny,  float nz,
                       float nvx, float nvy, float nvz,
                       bool  ngrounded, bool dirty) {
        reg.get<DynamicPositionComponent>(ent) = {nx, ny, nz, nvx, nvy, nvz, ngrounded};
        if (dirty) reg.get<DirtyComponent>(ent).mark(POSITION_BIT);
    }
};

} // namespace voxelmmo
