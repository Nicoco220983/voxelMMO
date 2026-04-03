#pragma once
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/GroundContactComponent.hpp"
#include "game/components/JumpComponent.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

/**
 * @brief Handles jump logic for entities with JumpComponent.
 * 
 * JumpSystem processes entities that can jump. It reads ground contact state
 * (set by PhysicsSystem) and applies jump impulses when conditions are met.
 * 
 * This separation keeps PhysicsSystem focused on physics simulation while
 * JumpSystem handles the game-logic decision of when to jump.
 * 
 * JumpSystem should run AFTER PhysicsSystem in the tick flow, as it needs:
 * - GroundContactComponent to be updated with current ground state
 * - DynamicPositionComponent to have post-physics velocity (including bounce)
 */
namespace JumpSystem {

/**
 * @brief Apply jump logic to all entities with JumpComponent.
 * 
 * For each entity:
 * 1. Check if grounded (via GroundContactComponent)
 * 2. If holding jump button and conditions met, add jump velocity
 * 3. Handle bounce+jump combination for slime blocks
 * 
 * @param registry    Entity registry
 * @param currentTick Current game tick (for cooldown tracking)
 * @param jumpVy      Base jump impulse velocity (e.g., PlayerEntity::PLAYER_JUMP_VY)
 */
void apply(entt::registry& registry, uint32_t currentTick, int32_t jumpVy);

/**
 * @brief Process jump logic for a single entity.
 * 
 * This is the main jump logic entry point. It checks:
 * - Is entity grounded?
 * - Is jump button held?
 * - Has cooldown expired?
 * - Should we add jump velocity to existing bounce?
 * 
 * Uses DynamicPositionComponent::modify() to ensure dirty flags are set properly.
 * 
 * @param registry    Entity registry
 * @param ent         Entity to process
 * @param dyn         Entity's dynamic position (read-only, modify() is called internally)
 * @param jump        Entity's jump component (will update lastJumpTick on success)
 * @param contact     Entity's ground contact component
 * @param currentTick Current game tick
 * @param jumpVy      Base jump impulse velocity
 * @return true if jump was executed
 */
bool processEntityJump(entt::registry& registry, entt::entity ent,
                       const DynamicPositionComponent& dyn,
                       JumpComponent& jump,
                       const GroundContactComponent& contact,
                       uint32_t currentTick, int32_t jumpVy);

} // namespace JumpSystem

} // namespace voxelmmo
