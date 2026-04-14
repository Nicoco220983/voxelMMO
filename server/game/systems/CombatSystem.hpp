#pragma once
#include "common/Types.hpp"
#include "common/ToolCatalog.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include <entt/entt.hpp>
#include <cstdint>
#include <vector>

namespace voxelmmo {

/**
 * @brief Unified combat system for melee attacks.
 *
 * Handles both player tool use and AI attacks with consistent
 * damage calculation, knockback, and cooldown enforcement.
 */
namespace CombatSystem {

/**
 * @brief Hit request parameters.
 */
struct HitRequest {
    entt::entity attacker{entt::null};
    entt::entity target{entt::null};
    uint16_t damage{0};
    float knockback{0.0f};      ///< Knockback impulse (sub-voxels/tick)
    float attackYaw{0.0f};      ///< Attack direction for knockback
};

/**
 * @brief Result of a hit attempt.
 */
struct HitResult {
    bool hit{false};            ///< Whether target was hit
    bool killed{false};         ///< Whether target was killed
    uint16_t damageDealt{0};    ///< Actual damage dealt
};

/**
 * @brief Process a single tool use from a player.
 *
 * - Validate tool cooldown via ToolComponent
 * - Raycast for target entity using bounding boxes
 * - Apply damage and knockback if target found
 *
 * @param registry       Entity registry.
 * @param playerEntities Map of PlayerId to entity.
 * @param playerId       Player using the tool.
 * @param toolId         Tool type ID.
 * @param yaw            Look direction yaw.
 * @param pitch          Look direction pitch.
 * @param currentTick    Current game tick.
 * @return true if tool use was processed (cooldown valid), false otherwise.
 */
bool processToolUse(entt::registry& registry,
                    const std::unordered_map<PlayerId, entt::entity>& playerEntities,
                    PlayerId playerId,
                    uint8_t toolId,
                    float yaw,
                    float pitch,
                    uint32_t currentTick);

/**
 * @brief Process an AI melee attack.
 *
 * Called by GoblinAISystem and other AI systems to perform attacks.
 * Bypasses tool/cooldown checks (AI manages its own cooldown).
 *
 * @param registry    Entity registry.
 * @param attacker    Attacking entity.
 * @param target      Target entity.
 * @param damage      Damage amount.
 * @param knockback   Knockback impulse.
 * @param attackYaw   Attack direction.
 * @param currentTick Current game tick.
 * @return HitResult  Result of the attack.
 */
HitResult processAIAttack(entt::registry& registry,
                          entt::entity attacker,
                          entt::entity target,
                          uint16_t damage,
                          float knockback,
                          float attackYaw,
                          uint32_t currentTick);

/**
 * @brief Apply a hit with damage and knockback.
 *
 * @param registry    Entity registry.
 * @param request     Hit parameters.
 * @param currentTick Current game tick.
 * @return HitResult  Result of the hit.
 */
HitResult applyHit(entt::registry& registry,
                   const HitRequest& request,
                   uint32_t currentTick);

/**
 * @brief Apply knockback velocity to an entity.
 *
 * @param registry        Entity registry.
 * @param target          Target entity.
 * @param attackYaw       Direction of knockback (radians).
 * @param knockbackSpeed  Velocity impulse (sub-voxels/tick).
 */
void applyKnockback(entt::registry& registry,
                    entt::entity target,
                    float attackYaw,
                    float knockbackSpeed);

/**
 * @brief Raycast to find an entity in front of the attacker.
 *
 * Uses bounding box intersection tests.
 *
 * @param registry      Entity registry.
 * @param origin        Attacker position.
 * @param yaw           Look direction yaw (radians).
 * @param pitch         Look direction pitch (radians).
 * @param maxRange      Maximum range in sub-voxels.
 * @param exclude       Entity to exclude (usually the attacker).
 * @return Entity hit, or entt::null if none.
 */
entt::entity raycastEntity(entt::registry& registry,
                           const DynamicPositionComponent& origin,
                           float yaw,
                           float pitch,
                           float maxRange,
                           entt::entity exclude);

} // namespace CombatSystem

} // namespace voxelmmo
