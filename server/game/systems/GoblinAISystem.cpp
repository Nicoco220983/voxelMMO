#include "game/systems/GoblinAISystem.hpp"
#include "game/systems/CombatSystem.hpp"
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

// Thresholds for velocity/yaw changes to reduce network spam
constexpr int32_t MIN_VELOCITY_DELTA = 1;    // ~0.2 voxels/tick
constexpr float MIN_YAW_DELTA = 0.1f;         // ~6 degrees
constexpr int32_t MIN_TARGET_POS_DELTA = 100; // ~0.4 voxels

void apply(entt::registry& reg, uint32_t currentTick) {
    auto view = reg.view<GoblinBehaviorComponent, DynamicPositionComponent, EntityTypeComponent>();
    
    view.each([&](entt::entity ent,
                  GoblinBehaviorComponent& behavior,
                  DynamicPositionComponent& dyn,
                  const EntityTypeComponent& etype)
    {
        if (etype.type != EntityType::GOBLIN) return;

        // Skip AI for dead entities
        if (const auto* health = reg.try_get<HealthComponent>(ent)) {
            if (health->current == 0) return;
        }

        // Skip AI updates while airborne (e.g., from knockback)
        // Let physics resolve knockback naturally; resume control when grounded
        if (!dyn.grounded) return;

        // ========== Helper Lambdas ==========
        
        auto findNearestPlayer = [&]() -> std::pair<entt::entity, int32_t> {
            entt::entity nearest = entt::null;
            int32_t nearestDistSq = GOBLIN_AGGRO_RADIUS * GOBLIN_AGGRO_RADIUS;
            
            auto playerView = reg.view<PlayerComponent, DynamicPositionComponent>();
            for (auto playerEnt : playerView) {
                const auto& playerPos = playerView.get<DynamicPositionComponent>(playerEnt);
                const int32_t dx = playerPos.x - dyn.x;
                const int32_t dy = playerPos.y - dyn.y;
                const int32_t dz = playerPos.z - dyn.z;
                const int32_t distSq = dx*dx + dy*dy + dz*dz;
                
                if (distSq < nearestDistSq) {
                    nearestDistSq = distSq;
                    nearest = playerEnt;
                }
            }
            return {nearest, nearestDistSq};
        };

        auto getGlobalId = [&](entt::entity target) -> uint32_t {
            if (target == entt::null || !reg.valid(target)) return 0;
            return reg.get<GlobalEntityIdComponent>(target).id;
        };

        auto findByGlobalId = [&](uint32_t globalId) -> entt::entity {
            if (globalId == 0) return entt::null;
            auto gidView = reg.view<GlobalEntityIdComponent>();
            for (auto e : gidView) {
                if (gidView.get<GlobalEntityIdComponent>(e).id == globalId) return e;
            }
            return entt::null;
        };

        // Compute chase motion. Returns true if velocity/yaw changed enough.
        auto computeChaseMotion = [&](const DynamicPositionComponent& targetPos, int32_t speed,
                                      int32_t& outVx, int32_t& outVz, float& outYaw) -> bool {
            const int32_t dx = targetPos.x - dyn.x;
            const int32_t dz = targetPos.z - dyn.z;
            const int32_t distSq = dx*dx + dz*dz;
            
            if (distSq == 0) {
                outVx = 0;
                outVz = 0;
                outYaw = behavior.yaw;
                return (dyn.vx != 0 || dyn.vz != 0);
            }
            
            const float yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
            const float dist = std::sqrt(static_cast<float>(distSq));
            const int32_t vx = static_cast<int32_t>((dx / dist) * speed);
            const int32_t vz = static_cast<int32_t>((dz / dist) * speed);
            
            const bool vxChanged = std::abs(vx - dyn.vx) > MIN_VELOCITY_DELTA;
            const bool vzChanged = std::abs(vz - dyn.vz) > MIN_VELOCITY_DELTA;
            const bool yawChanged = std::abs(yaw - behavior.yaw) > MIN_YAW_DELTA;
            
            outVx = vxChanged ? vx : dyn.vx;
            outVz = vzChanged ? vz : dyn.vz;
            outYaw = yawChanged ? yaw : behavior.yaw;
            
            return vxChanged || vzChanged || yawChanged;
        };

        auto enterChase = [&](entt::entity targetEnt, const DynamicPositionComponent& targetPos) {
            int32_t newVx, newVz;
            float newYaw;
            bool motionChanged = computeChaseMotion(targetPos, GOBLIN_CHASE_SPEED, newVx, newVz, newYaw);
            bool stateChanged = behavior.state != GoblinBehaviorComponent::State::CHASE;
            
            if (stateChanged || motionChanged) {
                GoblinBehaviorComponent::modifyWithTarget(reg, ent,
                    GoblinBehaviorComponent::State::CHASE,
                    currentTick + 10,
                    targetPos.x, targetPos.z,
                    newYaw,
                    getGlobalId(targetEnt),
                    currentTick + AGGRO_TIMEOUT_TICKS,
                    /*dirty=*/true);
                
                DynamicPositionComponent::modify(reg, ent,
                    dyn.x, dyn.y, dyn.z,
                    newVx, dyn.vy, newVz,
                    dyn.grounded,
                    /*dirty=*/true);
            }
        };

        auto enterAttack = [&](const DynamicPositionComponent& targetPos) {
            int32_t newVx, newVz;
            float newYaw;
            computeChaseMotion(targetPos, GOBLIN_CHASE_SPEED, newVx, newVz, newYaw);
            
            GoblinBehaviorComponent::modifyWithTarget(reg, ent,
                GoblinBehaviorComponent::State::ATTACK,
                currentTick + 5,
                targetPos.x, targetPos.z,
                newYaw,
                behavior.targetEntityId,
                currentTick + AGGRO_TIMEOUT_TICKS,
                /*dirty=*/true);
            
            DynamicPositionComponent::modify(reg, ent,
                dyn.x, dyn.y, dyn.z,
                0, dyn.vy, 0,
                dyn.grounded,
                /*dirty=*/true);
        };

        auto enterIdle = [&]() {
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
        };

        auto extendAttack = [&](const DynamicPositionComponent& targetPos) {
            bool targetChanged = std::abs(behavior.targetX - targetPos.x) > MIN_TARGET_POS_DELTA || 
                                 std::abs(behavior.targetZ - targetPos.z) > MIN_TARGET_POS_DELTA;
            
            if (targetChanged) {
                GoblinBehaviorComponent::modifyWithTarget(reg, ent,
                    GoblinBehaviorComponent::State::ATTACK,
                    currentTick + 5,
                    targetPos.x, targetPos.z,
                    behavior.yaw,
                    behavior.targetEntityId,
                    currentTick + AGGRO_TIMEOUT_TICKS,
                    /*dirty=*/true);
            }
        };

        auto tryAttackDamage = [&]() {
            entt::entity targetEnt = findByGlobalId(behavior.targetEntityId);
            if (targetEnt == entt::null || !reg.valid(targetEnt)) return;
            
            const auto& targetPos = reg.get<DynamicPositionComponent>(targetEnt);
            int32_t dx = targetPos.x - dyn.x;
            int32_t dy = targetPos.y - dyn.y;
            int32_t dz = targetPos.z - dyn.z;
            int32_t distSq = dx*dx + dy*dy + dz*dz;
            
            if (distSq <= GOBLIN_ATTACK_RADIUS * GOBLIN_ATTACK_RADIUS) {
                CombatSystem::processAIAttack(reg, ent, targetEnt,
                    GOBLIN_ATTACK_DAMAGE, GOBLIN_KNOCKBACK, behavior.yaw, currentTick);
            }
        };

        // ========== Main AI Logic ==========
        
        // First: determine if we should be chasing someone
        auto [nearestPlayer, nearestDistSq] = findNearestPlayer();
        bool shouldChase = nearestPlayer != entt::null;
        
        // Handle based on current state
        switch (behavior.state) {
            case GoblinBehaviorComponent::State::IDLE:
            case GoblinBehaviorComponent::State::WALKING: {
                if (shouldChase) {
                    // Switch to chase mode immediately
                    const auto& playerPos = reg.get<DynamicPositionComponent>(nearestPlayer);
                    enterChase(nearestPlayer, playerPos);
                } else if (currentTick >= behavior.stateEndTick) {
                    // Wander logic - state transition
                    uint32_t nextDuration = 120 + (currentTick % 181);
                    
                    if (behavior.state == GoblinBehaviorComponent::State::IDLE) {
                        // Pick random wander target
                        int32_t radius = 5 * SUBVOXEL_SIZE;
                        int32_t dx = (static_cast<int32_t>(currentTick * 12345) % (2 * radius)) - radius;
                        int32_t dz = (static_cast<int32_t>(currentTick * 67890) % (2 * radius)) - radius;
                        int32_t targetX = dyn.x + dx;
                        int32_t targetZ = dyn.z + dz;
                        float yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
                        
                        float dist = std::sqrt(static_cast<float>(dx*dx + dz*dz));
                        int32_t vx = dist > 0 ? static_cast<int32_t>((dx / dist) * GOBLIN_WALK_SPEED) : 0;
                        int32_t vz = dist > 0 ? static_cast<int32_t>((dz / dist) * GOBLIN_WALK_SPEED) : 0;
                        
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
                        // Stop walking
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
                // Validate current target
                entt::entity targetEnt = findByGlobalId(behavior.targetEntityId);
                bool lostTarget = true;
                
                if (targetEnt != entt::null && reg.valid(targetEnt)) {
                    const auto& targetPos = reg.get<DynamicPositionComponent>(targetEnt);
                    int32_t dx = targetPos.x - dyn.x;
                    int32_t dy = targetPos.y - dyn.y;
                    int32_t dz = targetPos.z - dyn.z;
                    int32_t distSq = dx*dx + dy*dy + dz*dz;
                    
                    if (distSq <= GOBLIN_AGGRO_RADIUS * GOBLIN_AGGRO_RADIUS) {
                        lostTarget = false;
                        
                        if (distSq <= GOBLIN_ATTACK_RADIUS * GOBLIN_ATTACK_RADIUS) {
                            // In attack range - switch to attack
                            enterAttack(targetPos);
                        } else {
                            // Continue chasing - update motion with thresholds
                            enterChase(targetEnt, targetPos);
                        }
                    }
                }
                
                // Lost target or timeout
                if (lostTarget || currentTick >= behavior.aggroCooldownTick) {
                    enterIdle();
                }
                break;
            }

            case GoblinBehaviorComponent::State::ATTACK: {
                // Deal damage if cooldown expired
                if (currentTick >= behavior.attackCooldownTick) {
                    tryAttackDamage();
                    
                    if (behavior.attackCooldownTick != currentTick + ATTACK_COOLDOWN_TICKS) {
                        GoblinBehaviorComponent::setAttackCooldown(reg, ent, 
                            currentTick + ATTACK_COOLDOWN_TICKS, /*dirty=*/true);
                    }
                }
                
                // Transition out of attack state
                if (currentTick >= behavior.stateEndTick) {
                    if (shouldChase && nearestDistSq <= GOBLIN_AGGRO_RADIUS * GOBLIN_AGGRO_RADIUS) {
                        const auto& playerPos = reg.get<DynamicPositionComponent>(nearestPlayer);
                        
                        if (nearestDistSq <= GOBLIN_ATTACK_RADIUS * GOBLIN_ATTACK_RADIUS) {
                            // Still in attack range - extend attack
                            extendAttack(playerPos);
                        } else {
                            // Back to chase
                            enterChase(nearestPlayer, playerPos);
                        }
                    } else {
                        enterIdle();
                    }
                }
                break;
            }
        }
    });
}

} // namespace GoblinAISystem
} // namespace voxelmmo
