#include "game/systems/InputSystem.hpp"
#include "game/components/GroundContactComponent.hpp"
#include "game/components/JumpComponent.hpp"
#include "game/components/WalkComponent.hpp"
#include "common/VoxelPhysicProps.hpp"

namespace voxelmmo {
namespace InputSystem {

void apply(entt::registry& reg) {
    auto view = reg.view<InputComponent, DynamicPositionComponent, EntityTypeComponent>();
    view.each([&](entt::entity ent,
                  const InputComponent& inp,
                  DynamicPositionComponent& dyn,
                  const EntityTypeComponent& et)
    {
        int32_t nvx = dyn.vx, nvy = dyn.vy, nvz = dyn.vz;

        // Update JumpComponent wantsToJump based on button state
        if (auto* jump = reg.try_get<JumpComponent>(ent)) {
            jump->wantsToJump = (inp.buttons & static_cast<uint8_t>(InputButton::JUMP)) != 0;
        }

        // Get max speed from ground surface (if on special voxel like mud)
        uint16_t maxSpeedXZ = 0;
        if (auto* contact = reg.try_get<GroundContactComponent>(ent)) {
            if (contact->groundType != VoxelPhysicTypes::AIR) {
                const auto& props = getVoxelPhysicProps(contact->groundType);
                maxSpeedXZ = props.maxSpeedXZ;
            }
        }

        if (et.type == EntityType::GHOST_PLAYER) {
            GhostPlayerEntity::computeVelocity(inp, nvx, nvy, nvz);
        } else if (et.type == EntityType::PLAYER) {
            // Use WalkComponent for horizontal movement if available
            if (auto* walk = reg.try_get<WalkComponent>(ent)) {
                walk->computeVelocity(inp, nvx, nvz, maxSpeedXZ);
            } else {
                // Fallback to legacy method for entities without WalkComponent
                PlayerEntity::computeVelocity(inp, dyn, nvx, nvy, nvz, maxSpeedXZ);
            }
        }

        // Note: Jump is now handled in PhysicsSystem (auto-jump)
        // We don't set nvy here anymore - PhysicsSystem handles it when grounded + wantsToJump

        const bool dirty = (nvx != dyn.vx || nvy != dyn.vy || nvz != dyn.vz);
        DynamicPositionComponent::modify(reg, ent,
            dyn.x, dyn.y, dyn.z, nvx, nvy, nvz, dyn.grounded, dirty);
    });
}

} // namespace InputSystem
} // namespace voxelmmo
