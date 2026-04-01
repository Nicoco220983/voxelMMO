#pragma once
#include "game/components/SheepBehaviorComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>

namespace voxelmmo::SheepAISystem {

/**
 * @brief Update all sheep AI state machines.
 *
 * State changes every 2-5s (random). Velocity is set once on state change,
 * not recomputed each tick. Only marks dirty on state transitions.
 *
 * @param reg          Entity registry.
 * @param currentTick  Current server tick.
 */
void apply(entt::registry& reg, uint32_t currentTick);

} // namespace voxelmmo::SheepAISystem
