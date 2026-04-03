#include "game/systems/InputSystem.hpp"
#include "game/components/GroundContactComponent.hpp"
#include "game/components/JumpComponent.hpp"
#include "game/components/WalkComponent.hpp"
#include "common/VoxelPhysicProps.hpp"

namespace voxelmmo {
namespace InputSystem {

/// Climbing speed on ladders (sub-voxels/tick) - slightly slower than walking
inline constexpr int32_t CLIMB_SPEED = 50;

void apply(entt::registry& reg) {
    auto view = reg.view<InputComponent, DynamicPositionComponent, EntityTypeComponent>();
    view.each([&](entt::entity ent,
                  const InputComponent& inp,
                  DynamicPositionComponent& dyn,
                  const EntityTypeComponent& et)
    {
        int32_t nvx = dyn.vx, nvy = dyn.vy, nvz = dyn.vz;

        // Check if entity is climbing (center inside ladder)
        bool isClimbing = false;
        if (auto* contact = reg.try_get<GroundContactComponent>(ent)) {
            isClimbing = contact->isClimbing;
        }

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
            if (isClimbing) {
                // On ladder: horizontal movement normal, vertical controlled by JUMP/DESCEND
                // Use WalkComponent for horizontal only
                if (auto* walk = reg.try_get<WalkComponent>(ent)) {
                    walk->computeVelocity(inp, nvx, nvz, maxSpeedXZ);
                } else {
                    PlayerEntity::computeVelocity(inp, dyn, nvx, nvy, nvz, maxSpeedXZ);
                }
                
                // Vertical: JUMP = climb up, DESCEND = climb down
                const bool wantsUp   = (inp.buttons & static_cast<uint8_t>(InputButton::JUMP)) != 0;
                const bool wantsDown = (inp.buttons & static_cast<uint8_t>(InputButton::DESCEND)) != 0;
                
                if (wantsUp && !wantsDown) {
                    nvy = CLIMB_SPEED;
                } else if (wantsDown && !wantsUp) {
                    nvy = -CLIMB_SPEED;
                } else {
                    nvy = 0; // No vertical movement if both/neither pressed
                }
                
                // Disable normal jump processing while climbing
                if (auto* jump = reg.try_get<JumpComponent>(ent)) {
                    jump->wantsToJump = false;
                }
            } else {
                // Normal movement (not climbing)
                if (auto* walk = reg.try_get<WalkComponent>(ent)) {
                    walk->computeVelocity(inp, nvx, nvz, maxSpeedXZ);
                } else {
                    PlayerEntity::computeVelocity(inp, dyn, nvx, nvy, nvz, maxSpeedXZ);
                }
            }
        }

        const bool dirty = (nvx != dyn.vx || nvy != dyn.vy || nvz != dyn.vz);
        DynamicPositionComponent::modify(reg, ent,
            dyn.x, dyn.y, dyn.z, nvx, nvy, nvz, dyn.grounded, dirty);
    });
}

} // namespace InputSystem
} // namespace voxelmmo
