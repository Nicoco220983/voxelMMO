#pragma once
#include "DirtyComponent.hpp"
#include "common/Types.hpp"
#include "common/SafeBufWriter.hpp"
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
    SubVoxelCoord x{0},  y{0},  z{0};   ///< World-space position (sub-voxels), always current.
    SubVoxelCoord vx{0}, vy{0}, vz{0};  ///< Velocity (sub-voxels per tick).
    bool grounded{false};          ///< Physics output: true = entity rests on solid ground.
                                   ///< Client uses this to suppress quadratic gravity prediction.
                                   ///< GHOST entities always output true; FLYING always false.
    bool moved{true};              ///< Set whenever position changes; cleared by checkEntitiesChunks().

    /**
     * @brief Serialize the raw position fields (no component-flags byte).
     *
     * Wire layout: int32 x,y,z | int32 vx,vy,vz | uint8 grounded
     * The caller is responsible for writing the component-flags byte beforehand.
     */
    void serializeFields(SafeBufWriter& w) const noexcept {
        w.write(x);  w.write(y);  w.write(z);
        w.write(vx); w.write(vy); w.write(vz);
        w.write<uint8_t>(grounded ? 1u : 0u);
    }

    /**
     * @brief Overwrite all fields.
     * @param dirty  When true, marks POSITION_BIT dirty so a delta is sent to clients.
     *               Pass false for routine per-tick position advances.
     */
    static void modify(entt::registry& reg, entt::entity ent,
                       SubVoxelCoord nx,  SubVoxelCoord ny,  SubVoxelCoord nz,
                       SubVoxelCoord nvx, SubVoxelCoord nvy, SubVoxelCoord nvz,
                       bool    ngrounded, bool dirty) {
        auto& c = reg.get<DynamicPositionComponent>(ent);
        c = {nx, ny, nz, nvx, nvy, nvz, ngrounded, /*moved=*/true};
        if (dirty) reg.get<DirtyComponent>(ent).mark(POSITION_BIT);
    }
};

} // namespace voxelmmo
