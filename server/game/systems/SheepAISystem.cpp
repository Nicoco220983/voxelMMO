#include "game/systems/SheepAISystem.hpp"
#include "game/entities/SheepEntity.hpp"
#include "game/components/HealthComponent.hpp"
#include <entt/entt.hpp>
#include <cmath>

namespace voxelmmo {
namespace SheepAISystem {

void apply(entt::registry& reg, uint32_t currentTick) {
    auto view = reg.view<SheepBehaviorComponent, DynamicPositionComponent, EntityTypeComponent>();
    
    view.each([&](entt::entity ent,
                  SheepBehaviorComponent& behavior,
                  DynamicPositionComponent& dyn,
                  const EntityTypeComponent& etype)
    {
        if (etype.type != EntityType::SHEEP) return;

        // Skip AI for dead entities
        if (const auto* health = reg.try_get<HealthComponent>(ent)) {
            if (health->current == 0) return;
        }

        // Only act on state transitions
        if (currentTick < behavior.stateEndTick) return;

        // Random duration for next state: 2-5s (120-300 ticks)
        const uint32_t nextDuration = 120 + (currentTick % 181);

        if (behavior.state == SheepBehaviorComponent::State::IDLE) {
            // IDLE -> WALKING: pick random target and set velocity
            const int32_t radius = 5 * SUBVOXEL_SIZE;
            const int32_t dx = (static_cast<int32_t>(currentTick * 12345) % (2 * radius)) - radius;
            const int32_t dz = (static_cast<int32_t>(currentTick * 67890) % (2 * radius)) - radius;
            
            const int32_t targetX = dyn.x + dx;
            const int32_t targetZ = dyn.z + dz;
            const float yaw = std::atan2(static_cast<float>(dx), static_cast<float>(dz));
            
            const float dist = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            const int32_t vx = dist > 0 ? static_cast<int32_t>((dx / dist) * SheepEntity::SHEEP_WALK_SPEED) : 0;
            const int32_t vz = dist > 0 ? static_cast<int32_t>((dz / dist) * SheepEntity::SHEEP_WALK_SPEED) : 0;
            
            SheepBehaviorComponent::modify(reg, ent,
                SheepBehaviorComponent::State::WALKING,
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
            SheepBehaviorComponent::modify(reg, ent,
                SheepBehaviorComponent::State::IDLE,
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
    });
}

} // namespace SheepAISystem
} // namespace voxelmmo
