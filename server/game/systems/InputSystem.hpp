#pragma once
#include "game/components/InputComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "common/NetworkProtocol.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>

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
            GhostPlayerEntity::computeVelocity(inp, nvx, nvy, nvz);
        } else if (et.type == EntityType::PLAYER) {
            PlayerEntity::computeVelocity(inp, dyn, nvx, nvy, nvz);
        }

        const bool dirty = (nvx != dyn.vx || nvy != dyn.vy || nvz != dyn.vz);
        DynamicPositionComponent::modify(reg, ent,
            dyn.x, dyn.y, dyn.z, nvx, nvy, nvz, dyn.grounded, dirty);
    });
}

} // namespace voxelmmo::InputSystem
