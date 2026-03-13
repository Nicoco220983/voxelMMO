#pragma once
#include "game/components/InputComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "common/NetworkProtocol.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>
#include <cmath>

namespace voxelmmo::InputSystem {

inline void apply(entt::registry& reg) {
    auto view = reg.view<InputComponent, DynamicPositionComponent, EntityTypeComponent>();
    view.each([&](entt::entity ent,
                  const InputComponent& inp,
                  DynamicPositionComponent& dyn,
                  const EntityTypeComponent& et)
    {
        const uint8_t b = inp.buttons;
        int32_t nvx = dyn.vx, nvy = dyn.vy, nvz = dyn.vz;

        if (et.type == EntityType::GHOST_PLAYER) {
            // 3-D flight: W/S follow pitch+yaw, JUMP=up, DESCEND=down
            const float cy = std::cos(inp.yaw),   sy = std::sin(inp.yaw);
            const float cp = std::cos(inp.pitch),  sp = std::sin(inp.pitch);
            float dx = 0, dy = 0, dz = 0;
            if (b & static_cast<uint8_t>(InputButton::FORWARD))  { dx += -sy*cp; dy += sp; dz += -cy*cp; }
            if (b & static_cast<uint8_t>(InputButton::BACKWARD)) { dx -= -sy*cp; dy -= sp; dz -= -cy*cp; }
            if (b & static_cast<uint8_t>(InputButton::LEFT))     { dx -= cy;              dz -= -sy;     }
            if (b & static_cast<uint8_t>(InputButton::RIGHT))    { dx += cy;              dz += -sy;     }
            if (b & static_cast<uint8_t>(InputButton::JUMP))     { dy += 1.0f; }
            if (b & static_cast<uint8_t>(InputButton::DESCEND))  { dy -= 1.0f; }
            const float len = std::sqrt(dx*dx + dy*dy + dz*dz);
            const float s   = (len > 0.001f) ? (static_cast<float>(GHOST_MOVE_SPEED) / len) : 0.0f;
            nvx = static_cast<int32_t>(dx * s);
            nvy = static_cast<int32_t>(dy * s);
            nvz = static_cast<int32_t>(dz * s);

        } else if (et.type == EntityType::PLAYER) {
            // Horizontal-only: W/S/A/D ignore pitch; Space = jump impulse when grounded
            const float cy = std::cos(inp.yaw), sy = std::sin(inp.yaw);
            float dx = 0, dz = 0;
            if (b & static_cast<uint8_t>(InputButton::FORWARD))  { dx += -sy; dz += -cy; }
            if (b & static_cast<uint8_t>(InputButton::BACKWARD)) { dx -= -sy; dz -= -cy; }
            if (b & static_cast<uint8_t>(InputButton::LEFT))     { dx -= cy;  dz -= -sy; }
            if (b & static_cast<uint8_t>(InputButton::RIGHT))    { dx += cy;  dz += -sy; }
            const float hlen = std::sqrt(dx*dx + dz*dz);
            const float hs   = (hlen > 0.001f) ? (static_cast<float>(PLAYER_WALK_SPEED) / hlen) : 0.0f;
            nvx = static_cast<int32_t>(dx * hs);
            nvz = static_cast<int32_t>(dz * hs);
            // Jump: impulse when grounded; vy otherwise owned by physics (gravity)
            if ((b & static_cast<uint8_t>(InputButton::JUMP)) && dyn.grounded)
                nvy = PLAYER_JUMP_VY;
        }

        const bool dirty = (nvx != dyn.vx || nvy != dyn.vy || nvz != dyn.vz);
        DynamicPositionComponent::modify(reg, ent,
            dyn.x, dyn.y, dyn.z, nvx, nvy, nvz, dyn.grounded, dirty);
    });
}

} // namespace voxelmmo::InputSystem
