#pragma once
#include <entt/entt.hpp>
#include <cstdint>

namespace voxelmmo {
namespace GoblinAISystem {

/**
 * @brief Goblin AI: wanders, detects players, chases and attacks.
 *
 * State machine:
 *   - IDLE/WALKING: Wandering randomly like sheep
 *   - CHASE: Moving toward target player at high speed
 *   - ATTACK: Performing melee attack (brief state)
 *
 * Detection:
 *   - Each tick, scan for players within GOBLIN_AGGRO_RADIUS
 *   - Switch to CHASE if player found (closest player)
 *
 * Attack:
 *   - When within GOBLIN_ATTACK_RADIUS, switch to ATTACK
 *   - Deal damage via HealthComponent::applyDamage()
 *   - Attack cooldown: GOBLIN_ATTACK_COOLDOWN_TICKS
 *
 * @param reg         Entity registry.
 * @param currentTick Current server tick.
 */
void apply(entt::registry& reg, uint32_t currentTick);

} // namespace GoblinAISystem
} // namespace voxelmmo
