#include "game/systems/GoblinAISystem.hpp"
#include "game/entities/GoblinEntity.hpp"
#include "game/components/GoblinBehaviorComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/HealthComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include <entt/entt.hpp>
#include <cmath>

namespace voxelmmo {
namespace GoblinAISystem {

using namespace GoblinEntity;

void apply(entt::registry& reg, uint32_t currentTick) {
    auto view = reg.view<GoblinBehaviorComponent, DynamicPositionComponent, EntityTypeComponent>();
    
    view.each([&](entt::entity ent,
                  GoblinBehaviorComponent& behavior,
                  DynamicPositionComponent& dyn,
                  const EntityTypeComponent& etype)
    {
        if (etype.type != EntityType::GOBLIN) return;

        // Helper lambda to find nearest player within aggro radius
        auto findNearestPlayer = [&]() -> std::pair<entt::entity, int32_t> {
            entt::entity nearest = entt::null;
            int32_t nearestDistSq = GOBLIN_AGGRO_RADIUS * GOBLIN_AGGRO_RADIUS;
            
            auto playerView = reg.view<PlayerComponent, DynamicPositionComponent>();
            for (auto playerEnt : playerView) {
                const auto& playerPos = playerView.get<DynamicPositionComponent>(playerEnt);
                const int32_t dx = playerPos.x - dyn.x;
                const int32_t dy = playerPos.y - dyn.y;
                const int32_t dz = playerPos.z - dyn.z;
                const int32_t distSq = dx * dx + dy * dy + dz * dz;
                
                if (distSq < nearestDistSq) {
                    nearestDistSq = distSq;
                    nearest = playerEnt;
                }
            }
            return {nearest, nearestDistSq};
        };

        // Helper to get GlobalEntityId from entity
        auto getGlobalId = [&](entt::entity target) -> uint32_t {
            if (target == entt::null || !reg.valid(target)) return 0;
            const auto& gid = reg.get<GlobalEntityIdComponent>(target);
            return gid.id;
        };

        // State machine
        switch (behavior.state) {
            case GoblinBehaviorComponent::State::IDLE:
            case GoblinBehaviorComponent::State::WALKING: {
                // Check for nearby players first (highest priority)
                auto [nearestPlayer, distSq] = findNearestPlayer();
                
                if (nearestPlayer != entt::null) {
                    // Player detected! Switch to CHASE
                    const auto& playerPos = reg.get<DynamicPositionComponent>(nearestPlayer);
                    const int32_t dx = playerPos.x - dyn.x;
                    const int32_t dz = playerPos.z - dyn.z;
                    const float yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
                    
                    GoblinBehaviorComponent::modifyWithTarget(reg, ent,
                        GoblinBehaviorComponent::State::CHASE,
                        currentTick + 10,  // Re-evaluate chase soon
                        playerPos.x, playerPos.z,
                        yaw,
                        getGlobalId(nearestPlayer),
                        currentTick + AGGRO_TIMEOUT_TICKS,
                        /*dirty=*/true);
                    
                    // Set chase velocity
                    const float dist = std::sqrt(static_cast<float>(distSq));
                    const int32_t vx = dist > 0 ? static_cast<int32_t>((dx / dist) * GOBLIN_CHASE_SPEED) : 0;
                    const int32_t vz = dist > 0 ? static_cast<int32_t>((dz / dist) * GOBLIN_CHASE_SPEED) : 0;
                    
                    DynamicPositionComponent::modify(reg, ent,
                        dyn.x, dyn.y, dyn.z,
                        vx, dyn.vy, vz,
                        dyn.grounded,
                        /*dirty=*/true);
                    return;
                }

                // No player nearby - wander like sheep
                if (currentTick >= behavior.stateEndTick) {
                    const uint32_t nextDuration = 120 + (currentTick % 181);  // 2-5s

                    if (behavior.state == GoblinBehaviorComponent::State::IDLE) {
                        // IDLE -> WALKING: pick random target
                        const int32_t radius = 5 * SUBVOXEL_SIZE;
                        const int32_t dx = (static_cast<int32_t>(currentTick * 12345) % (2 * radius)) - radius;
                        const int32_t dz = (static_cast<int32_t>(currentTick * 67890) % (2 * radius)) - radius;
                        
                        const int32_t targetX = dyn.x + dx;
                        const int32_t targetZ = dyn.z + dz;
                        const float yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
                        
                        const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
                        const int32_t vx = dist > 0 ? static_cast<int32_t>((dx / dist) * GOBLIN_WALK_SPEED) : 0;
                        const int32_t vz = dist > 0 ? static_cast<int32_t>((dz / dist) * GOBLIN_WALK_SPEED) : 0;
                        
                        GoblinBehaviorComponent::modify(reg, ent,
                            GoblinBehaviorComponent::State::WALKING,
                            currentTick + nextDuration,
                            targetX, targetZ,
                            yaw,
                            /*dirty=*/true);
                        
                        DynamicPositionComponent::modify(reg, ent,
                            dyn.x, dyn.y, dyn.z,
                            vx, dyn.vy, vz,
                            dyn.grounded,
                            /*dirty=*/true);
                    } else {
                        // WALKING -> IDLE: stop moving
                        GoblinBehaviorComponent::modify(reg, ent,
                            GoblinBehaviorComponent::State::IDLE,
                            currentTick + nextDuration,
                            dyn.x, dyn.z,
                            behavior.yaw,
                            /*dirty=*/true);
                        
                        DynamicPositionComponent::modify(reg, ent,
                            dyn.x, dyn.y, dyn.z,
                            0, dyn.vy, 0,
                            dyn.grounded,
                            /*dirty=*/true);
                    }
                }
                break;
            }

            case GoblinBehaviorComponent::State::CHASE: {
                // Check if target still valid and within aggro range
                bool lostTarget = true;
                entt::entity targetEnt = entt::null;
                
                // Find target entity by GlobalEntityId
                if (behavior.targetEntityId != 0) {
                    auto gidView = reg.view<GlobalEntityIdComponent>();
                    for (auto e : gidView) {
                        if (gidView.get<GlobalEntityIdComponent>(e).id == behavior.targetEntityId) {
                            targetEnt = e;
                            break;
                        }
                    }
                }

                if (targetEnt != entt::null && reg.valid(targetEnt)) {
                    const auto& targetPos = reg.get<DynamicPositionComponent>(targetEnt);
                    const int32_t dx = targetPos.x - dyn.x;
                    const int32_t dy = targetPos.y - dyn.y;
                    const int32_t dz = targetPos.z - dyn.z;
                    const int32_t distSq = dx * dx + dy * dy + dz * dz;

                    if (distSq <= GOBLIN_AGGRO_RADIUS * GOBLIN_AGGRO_RADIUS) {
                        lostTarget = false;
                        
                        // Check if in attack range
                        if (distSq <= GOBLIN_ATTACK_RADIUS * GOBLIN_ATTACK_RADIUS) {
                            // Switch to ATTACK
                            const float yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
                            
                            GoblinBehaviorComponent::modifyWithTarget(reg, ent,
                                GoblinBehaviorComponent::State::ATTACK,
                                currentTick + 5,  // Brief attack state (0.25s)
                                targetPos.x, targetPos.z,
                                yaw,
                                behavior.targetEntityId,
                                currentTick + AGGRO_TIMEOUT_TICKS,
                                /*dirty=*/true);
                            
                            // Stop moving for attack
                            DynamicPositionComponent::modify(reg, ent,
                                dyn.x, dyn.y, dyn.z,
                                0, dyn.vy, 0,
                                dyn.grounded,
                                /*dirty=*/true);
                            return;
                        }

                        // Continue chasing - update direction toward target
                        const float yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
                        const float dist = std::sqrt(static_cast<float>(distSq));
                        const int32_t vx = dist > 0 ? static_cast<int32_t>((dx / dist) * GOBLIN_CHASE_SPEED) : 0;
                        const int32_t vz = dist > 0 ? static_cast<int32_t>((dz / dist) * GOBLIN_CHASE_SPEED) : 0;
                        
                        // Update chase target and extend aggro timeout
                        GoblinBehaviorComponent::modifyWithTarget(reg, ent,
                            GoblinBehaviorComponent::State::CHASE,
                            currentTick + 10,  // Re-evaluate soon
                            targetPos.x, targetPos.z,
                            yaw,
                            behavior.targetEntityId,
                            currentTick + AGGRO_TIMEOUT_TICKS,
                            /*dirty=*/true);
                        
                        DynamicPositionComponent::modify(reg, ent,
                            dyn.x, dyn.y, dyn.z,
                            vx, dyn.vy, vz,
                            dyn.grounded,
                            /*dirty=*/true);
                    }
                }

                // Lost target or timed out
                if (lostTarget || currentTick >= behavior.aggroCooldownTick) {
                    // Return to IDLE
                    GoblinBehaviorComponent::modify(reg, ent,
                        GoblinBehaviorComponent::State::IDLE,
                        currentTick + 60,  // 3s idle
                        dyn.x, dyn.z,
                        behavior.yaw,
                        /*dirty=*/true);
                    
                    DynamicPositionComponent::modify(reg, ent,
                        dyn.x, dyn.y, dyn.z,
                        0, dyn.vy, 0,
                        dyn.grounded,
                        /*dirty=*/true);
                }
                break;
            }

            case GoblinBehaviorComponent::State::ATTACK: {
                // Check if we can perform attack (cooldown expired)
                if (currentTick >= behavior.attackCooldownTick) {
                    // Find target entity
                    entt::entity targetEnt = entt::null;
                    if (behavior.targetEntityId != 0) {
                        auto gidView = reg.view<GlobalEntityIdComponent>();
                        for (auto e : gidView) {
                            if (gidView.get<GlobalEntityIdComponent>(e).id == behavior.targetEntityId) {
                                targetEnt = e;
                                break;
                            }
                        }
                    }

                    // Deal damage if target valid and still in range
                    if (targetEnt != entt::null && reg.valid(targetEnt)) {
                        const auto& targetPos = reg.get<DynamicPositionComponent>(targetEnt);
                        const int32_t dx = targetPos.x - dyn.x;
                        const int32_t dy = targetPos.y - dyn.y;
                        const int32_t dz = targetPos.z - dyn.z;
                        const int32_t distSq = dx * dx + dy * dy + dz * dz;

                        if (distSq <= GOBLIN_ATTACK_RADIUS * GOBLIN_ATTACK_RADIUS) {
                            // Deal damage
                            HealthComponent::applyDamage(reg, targetEnt, GOBLIN_ATTACK_DAMAGE, currentTick);
                        }
                    }

                    // Set next attack cooldown
                    GoblinBehaviorComponent::setAttackCooldown(reg, ent, 
                        currentTick + ATTACK_COOLDOWN_TICKS, /*dirty=*/true);
                }

                // Transition out of attack state
                if (currentTick >= behavior.stateEndTick) {
                    // Check if target still in aggro range
                    auto [nearestPlayer, distSq] = findNearestPlayer();
                    
                    if (nearestPlayer != entt::null && distSq <= GOBLIN_AGGRO_RADIUS * GOBLIN_AGGRO_RADIUS) {
                        // If still in attack range, stay in attack (extend) instead of moving closer
                        if (distSq <= GOBLIN_ATTACK_RADIUS * GOBLIN_ATTACK_RADIUS) {
                            // Extend attack state - don't move, wait for next attack cooldown
                            GoblinBehaviorComponent::modifyWithTarget(reg, ent,
                                GoblinBehaviorComponent::State::ATTACK,
                                currentTick + 5,  // Extend 0.25s
                                behavior.targetX, behavior.targetZ,
                                behavior.yaw,
                                behavior.targetEntityId,
                                currentTick + AGGRO_TIMEOUT_TICKS,
                                /*dirty=*/true);
                            // Keep velocity at 0 (already stopped)
                        } else {
                            // Return to CHASE (target moved out of attack range but in aggro)
                            const auto& playerPos = reg.get<DynamicPositionComponent>(nearestPlayer);
                            const int32_t dx = playerPos.x - dyn.x;
                            const int32_t dz = playerPos.z - dyn.z;
                            const float yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
                            
                            GoblinBehaviorComponent::modifyWithTarget(reg, ent,
                                GoblinBehaviorComponent::State::CHASE,
                                currentTick + 10,
                                playerPos.x, playerPos.z,
                                yaw,
                                getGlobalId(nearestPlayer),
                                currentTick + AGGRO_TIMEOUT_TICKS,
                                /*dirty=*/true);
                            
                            const float dist = std::sqrt(static_cast<float>(distSq));
                            const int32_t vx = dist > 0 ? static_cast<int32_t>((dx / dist) * GOBLIN_CHASE_SPEED) : 0;
                            const int32_t vz = dist > 0 ? static_cast<int32_t>((dz / dist) * GOBLIN_CHASE_SPEED) : 0;
                            
                            DynamicPositionComponent::modify(reg, ent,
                                dyn.x, dyn.y, dyn.z,
                                vx, dyn.vy, vz,
                                dyn.grounded,
                                /*dirty=*/true);
                        }
                    } else {
                        // Return to IDLE
                        GoblinBehaviorComponent::modify(reg, ent,
                            GoblinBehaviorComponent::State::IDLE,
                            currentTick + 60,
                            dyn.x, dyn.z,
                            behavior.yaw,
                            /*dirty=*/true);
                        
                        DynamicPositionComponent::modify(reg, ent,
                            dyn.x, dyn.y, dyn.z,
                            0, dyn.vy, 0,
                            dyn.grounded,
                            /*dirty=*/true);
                    }
                }
                break;
            }
        }
    });
}

} // namespace GoblinAISystem
} // namespace voxelmmo
