#pragma once
#include "game/components/InputComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/GroundContactComponent.hpp"
#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "common/NetworkProtocol.hpp"
#include "common/Types.hpp"
#include "common/VoxelPhysicType.hpp"
#include <entt/entt.hpp>

namespace voxelmmo::InputSystem {

void apply(entt::registry& reg);

} // namespace voxelmmo::InputSystem
