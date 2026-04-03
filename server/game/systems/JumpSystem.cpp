#include "game/systems/JumpSystem.hpp"
#include "common/VoxelPhysicProps.hpp"
#include <cmath>

namespace voxelmmo {
namespace JumpSystem {

bool processEntityJump(entt::registry& registry, entt::entity ent,
                       const DynamicPositionComponent& dyn,
                       JumpComponent& jump,
                       const GroundContactComponent& contact,
                       uint32_t currentTick, int32_t jumpVy) {
    // Must want to jump
    if (!jump.wantsToJump) {
        return false;
    }
    
    // Check if we just landed on a bouncy surface
    if (contact.justLanded && contact.isBouncing()) {
        // Add jump boost to existing bounce
        // PhysicsSystem already applied bounceVelocity to dyn.vy
        if (dyn.vy > 0) {
            const int32_t newVy = dyn.vy + jumpVy;
            jump.onJumpExecuted(currentTick);
            // Use modify() to ensure dirty flag is set
            DynamicPositionComponent::modify(registry, ent,
                dyn.x, dyn.y, dyn.z,
                dyn.vx, newVy, dyn.vz,
                dyn.grounded, /*dirty=*/true);
            return true;
        }
    }
    
    // Regular auto-jump: must be grounded, not already moving vertically, cooldown expired
    if (!contact.isGrounded()) {
        return false;
    }
    
    if (dyn.vy != 0) {
        return false; // Already moving vertically (could be from bounce)
    }
    
    if (!jump.canJump(currentTick)) {
        return false;
    }
    
    // Execute jump
    jump.onJumpExecuted(currentTick);
    // Use modify() to ensure dirty flag is set
    DynamicPositionComponent::modify(registry, ent,
        dyn.x, dyn.y, dyn.z,
        dyn.vx, jumpVy, dyn.vz,
        dyn.grounded, /*dirty=*/true);
    return true;
}

void apply(entt::registry& registry, uint32_t currentTick, int32_t jumpVy) {
    // Process entities that have both JumpComponent and GroundContactComponent
    auto view = registry.view<JumpComponent, GroundContactComponent, DynamicPositionComponent>();
    
    view.each([&](entt::entity ent,
                  JumpComponent& jump,
                  const GroundContactComponent& contact,
                  const DynamicPositionComponent& dyn) {
        processEntityJump(registry, ent, dyn, jump, contact, currentTick, jumpVy);
    });
}

} // namespace JumpSystem
} // namespace voxelmmo
