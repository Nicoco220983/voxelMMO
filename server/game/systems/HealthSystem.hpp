#pragma once
#include "game/components/HealthComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include <entt/entt.hpp>
#include <cstdint>

namespace voxelmmo {
namespace HealthSystem {

/**
 * @brief Check health-based deletion timeouts and mark expired ones for deletion.
 *
 * Entities with HealthComponent that have deleteAtTick set will be marked with
 * DELETE_ENTITY delta type when their TTL expires. This runs BEFORE serialization
 * so clients receive the DELETE delta.
 *
 * @param registry  The ECS registry.
 * @param tick      Current game tick.
 */
void processDeathTimeouts(entt::registry& registry, uint32_t tick);

} // namespace HealthSystem
} // namespace voxelmmo
