#include "game/systems/CombatSystem.hpp"
#include "game/components/ToolComponent.hpp"
#include "game/components/HealthComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include <cmath>
#include <limits>
#include <iostream>

namespace voxelmmo {
namespace CombatSystem {

bool processToolUse(entt::registry& registry,
                    const std::unordered_map<PlayerId, entt::entity>& playerEntities,
                    PlayerId playerId,
                    uint8_t toolId,
                    float yaw,
                    float pitch,
                    uint32_t currentTick) {
    const auto& catalog = ToolCatalog::instance();


    // Find player entity
    auto it = playerEntities.find(playerId);
    if (it == playerEntities.end()) {
        return false;
    }

    entt::entity attacker = it->second;
    if (!registry.valid(attacker)) {
        return false;
    }

    // Check if entity has ToolComponent
    auto* toolComp = registry.try_get<ToolComponent>(attacker);
    if (!toolComp) {
        return false;
    }


    // Validate tool matches
    if (toolComp->toolId != toolId) {
        return false;
    }

    // Check cooldown
    if (!toolComp->canUse(currentTick, catalog)) {
        return false;
    }

    // Get tool info
    const auto* toolInfo = catalog.findById(toolId);
    if (!toolInfo) {
        return false;
    }


    // Get attacker position
    auto* attackerPos = registry.try_get<DynamicPositionComponent>(attacker);
    if (!attackerPos) {
        return false;
    }


    // Raycast for target
    entt::entity target = raycastEntity(registry, *attackerPos,
                                        yaw, pitch,
                                        toolInfo->range,
                                        attacker);

    if (target == entt::null) {
        // Still mark tool as used (cooldown consumed even on miss)
        ToolComponent::markUsed(registry, attacker, currentTick, /*dirty=*/true);
        return true;
    }


    // Apply hit
    HitRequest request;
    request.attacker = attacker;
    request.target = target;
    request.damage = toolInfo->damage;
    request.knockback = toolInfo->knockback;
    request.attackYaw = yaw;

    HitResult result = applyHit(registry, request, currentTick);

    // Mark tool as used
    ToolComponent::markUsed(registry, attacker, currentTick, /*dirty=*/true);
    return true;
}

HitResult processAIAttack(entt::registry& registry,
                          entt::entity attacker,
                          entt::entity target,
                          uint16_t damage,
                          float knockback,
                          float attackYaw,
                          uint32_t currentTick) {
    HitRequest request;
    request.attacker = attacker;
    request.target = target;
    request.damage = damage;
    request.knockback = knockback;
    request.attackYaw = attackYaw;

    return applyHit(registry, request, currentTick);
}

HitResult applyHit(entt::registry& registry,
                   const HitRequest& request,
                   uint32_t currentTick) {
    HitResult result;

    if (!registry.valid(request.target)) {
        return result;
    }

    // Check if target has health
    auto* health = registry.try_get<HealthComponent>(request.target);
    if (!health) {
        return result;
    }

    // Apply damage
    bool killed = HealthComponent::applyDamage(registry, request.target,
                                               request.damage, currentTick);

    // Apply knockback
    if (request.knockback > 0.0f) {
        applyKnockback(registry, request.target, request.attackYaw, request.knockback);
    }

    result.hit = true;
    result.killed = killed;
    result.damageDealt = request.damage;

    return result;
}

void applyKnockback(entt::registry& registry,
                    entt::entity target,
                    float attackYaw,
                    float knockbackSpeed) {
    auto* dyn = registry.try_get<DynamicPositionComponent>(target);
    if (!dyn) return;

    // Calculate knockback velocity from yaw
    // Must match raycast convention: yaw 0 = -Z, yaw PI = +Z, yaw PI/2 = -X, -PI/2 = +X
    float vx = -std::sin(attackYaw) * knockbackSpeed;
    float vz = -std::cos(attackYaw) * knockbackSpeed;

    // Add slight upward impulse for "flinch" effect
    float vy = dyn->vy + knockbackSpeed * 0.3f;

    DynamicPositionComponent::modify(registry, target,
                                     dyn->x, dyn->y, dyn->z,
                                     static_cast<int32_t>(vx),
                                     static_cast<int32_t>(vy),
                                     static_cast<int32_t>(vz),
                                     dyn->grounded,
                                     /*dirty=*/true);
}

entt::entity raycastEntity(entt::registry& registry,
                           const DynamicPositionComponent& origin,
                           float yaw,
                           float pitch,
                           float maxRange,
                           entt::entity exclude) {
    // Calculate ray direction from yaw/pitch
    // MUST MATCH CLIENT: camera.getWorldDirection() in client/src/controllers/BaseController.js
    // Client pitch equals Three.js camera.rotation.x: positive = down, negative = up.
    // yaw: 0 = -Z (facing away from camera default), PI = +Z, PI/2 = -X, -PI/2 = +X
    float cosPitch = std::cos(pitch);
    float dirX = -std::sin(yaw) * cosPitch;
    float dirY = std::sin(pitch);             // pitch positive (look down) -> dirY negative (down)
    float dirZ = -std::cos(yaw) * cosPitch;

    // Normalize direction
    float len = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (len > 0.0f) {
        dirX /= len;
        dirY /= len;
        dirZ /= len;
    }

    // Ray origin is center of entity bounding box
    float rayOriginX = static_cast<float>(origin.x);
    float rayOriginY = static_cast<float>(origin.y);
    float rayOriginZ = static_cast<float>(origin.z);


    entt::entity closestHit = entt::null;
    float closestDist = maxRange;
    int entitiesChecked = 0;
    int entitiesWithHealth = 0;

    // Iterate all entities with bounding boxes
    auto view = registry.view<BoundingBoxComponent, DynamicPositionComponent>();
    for (auto [ent, bbox, pos] : view.each()) {
        entitiesChecked++;
        if (ent == exclude) continue;

        // Skip entities without health (can't be damaged)
        if (!registry.try_get<HealthComponent>(ent)) continue;
        entitiesWithHealth++;
        

        // Build AABB
        float minX = static_cast<float>(pos.x - bbox.hx);
        float maxX = static_cast<float>(pos.x + bbox.hx);
        float minY = static_cast<float>(pos.y - bbox.hy);
        float maxY = static_cast<float>(pos.y + bbox.hy);
        float minZ = static_cast<float>(pos.z - bbox.hz);
        float maxZ = static_cast<float>(pos.z + bbox.hz);


        // Ray-AABB intersection (slab method)
        float tmin = 0.0f;
        float tmax = maxRange;

        // X slab
        if (std::abs(dirX) < 1e-6f) {
            if (rayOriginX < minX || rayOriginX > maxX) {
                continue;
            }
        } else {
            float tx1 = (minX - rayOriginX) / dirX;
            float tx2 = (maxX - rayOriginX) / dirX;
            if (tx1 > tx2) std::swap(tx1, tx2);
            tmin = std::max(tmin, tx1);
            tmax = std::min(tmax, tx2);
            if (tmin > tmax) {
                continue;
            }
        }

        // Y slab
        if (std::abs(dirY) < 1e-6f) {
            if (rayOriginY < minY || rayOriginY > maxY) {
                continue;
            }
        } else {
            float ty1 = (minY - rayOriginY) / dirY;
            float ty2 = (maxY - rayOriginY) / dirY;
            if (ty1 > ty2) std::swap(ty1, ty2);
            tmin = std::max(tmin, ty1);
            tmax = std::min(tmax, ty2);
            if (tmin > tmax) {
                continue;
            }
        }

        // Z slab
        if (std::abs(dirZ) < 1e-6f) {
            if (rayOriginZ < minZ || rayOriginZ > maxZ) {
                continue;
            }
        } else {
            float tz1 = (minZ - rayOriginZ) / dirZ;
            float tz2 = (maxZ - rayOriginZ) / dirZ;
            if (tz1 > tz2) std::swap(tz1, tz2);
            tmin = std::max(tmin, tz1);
            tmax = std::min(tmax, tz2);
            if (tmin > tmax) {
                continue;
            }
        }

        // Hit found - check if closer than current closest
        if (tmin < closestDist && tmin >= 0.0f) {
            closestDist = tmin;
            closestHit = ent;
        }
    }


    return closestHit;
}

} // namespace CombatSystem
} // namespace voxelmmo
