#pragma once
#include "game/components/SheepBehaviorComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/entities/SheepEntity.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>
#include <cmath>

namespace voxelmmo::SheepAISystem {

/**
 * @brief Update all sheep AI state machines.
 *
 * IDLE → WALKING: After random 2-5s delay, pick random target within 5-voxel radius.
 * WALKING → IDLE: After 2s (120 ticks), stop and enter idle.
 *
 * During WALKING: Set velocity toward target, face movement direction.
 *
 * @param reg          Entity registry.
 * @param currentTick  Current server tick.
 */
inline void apply(entt::registry& reg, uint32_t currentTick) {
    auto view = reg.view<SheepBehaviorComponent, DynamicPositionComponent, EntityTypeComponent>();
    
    view.each([&](entt::entity ent,
                  SheepBehaviorComponent& behavior,
                  DynamicPositionComponent& dyn,
                  const EntityTypeComponent& etype)
    {
        if (etype.type != EntityType::SHEEP) return;

        const bool shouldTransition = currentTick >= behavior.stateEndTick;

        if (behavior.state == SheepBehaviorComponent::State::IDLE) {
            if (shouldTransition) {
                // Pick random target within 5-voxel radius (1280 sub-voxels)
                const int32_t radius = 5 * SUBVOXEL_SIZE;
                const int32_t dx = (static_cast<int32_t>(currentTick * 12345) % (2 * radius)) - radius;
                const int32_t dz = (static_cast<int32_t>(currentTick * 67890) % (2 * radius)) - radius;
                
                behavior.targetX = dyn.x + dx;
                behavior.targetZ = dyn.z + dz;
                
                // Face target
                behavior.yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
                
                // Enter walking state for 2s (120 ticks)
                SheepBehaviorComponent::modify(reg, ent,
                    SheepBehaviorComponent::State::WALKING,
                    currentTick + 120,
                    behavior.targetX, behavior.targetZ,
                    behavior.yaw,
                    /*dirty=*/true);
            }
        } else { // WALKING
            if (shouldTransition) {
                // Stop moving and enter idle for 2-5s
                const uint32_t idleTicks = 120 + (currentTick % 180);
                
                // Clear velocity
                DynamicPositionComponent::modify(reg, ent,
                    dyn.x, dyn.y, dyn.z,
                    0, dyn.vy, 0,  // Keep vy for gravity
                    dyn.grounded,
                    /*dirty=*/true);
                
                SheepBehaviorComponent::modify(reg, ent,
                    SheepBehaviorComponent::State::IDLE,
                    currentTick + idleTicks,
                    dyn.x, dyn.z,
                    behavior.yaw,
                    /*dirty=*/true);
            } else {
                // Continue walking toward target
                const int32_t dx = behavior.targetX - dyn.x;
                const int32_t dz = behavior.targetZ - dyn.z;
                const int32_t distSq = dx * dx + dz * dz;
                
                // Stop if close to target
                if (distSq < 256 * 256) {  // Within 1 voxel
                    SheepBehaviorComponent::modify(reg, ent,
                        SheepBehaviorComponent::State::IDLE,
                        currentTick,  // Transition immediately
                        dyn.x, dyn.z,
                        behavior.yaw,
                        /*dirty=*/true);
                } else {
                    // Set velocity toward target (no dirty marking during walking)
                    const float dist = std::sqrt(static_cast<float>(distSq));
                    const int32_t vx = static_cast<int32_t>((dx / dist) * SheepEntity::SHEEP_WALK_SPEED);
                    const int32_t vz = static_cast<int32_t>((dz / dist) * SheepEntity::SHEEP_WALK_SPEED);
                    
                    // Update yaw to face movement direction (naive: don't mark dirty, client can interpolate)
                    behavior.yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
                    
                    DynamicPositionComponent::modify(reg, ent,
                        dyn.x, dyn.y, dyn.z,
                        vx, dyn.vy, vz,
                        dyn.grounded,
                        /*dirty=*/false);  // Position dirty bit handled by physics
                }
            }
        }
    });
}

} // namespace voxelmmo::SheepAISystem
