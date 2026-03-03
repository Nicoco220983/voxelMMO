#pragma once
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/InputComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/ChunkMemberComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"
#include "common/MessageTypes.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>

namespace voxelmmo::PlayerEntity {

/** Full-physics player: gravity + swept AABB collision. */
inline void spawn(entt::registry& reg, entt::entity ent,
                  int32_t x, int32_t y, int32_t z, PlayerId playerId)
{
    reg.emplace<DynamicPositionComponent>(ent, x, y, z, 0, 0, 0, /*grounded=*/false);
    reg.emplace<DirtyComponent>(ent);
    reg.emplace<EntityTypeComponent>(ent, EntityType::PLAYER);
    reg.emplace<InputComponent>(ent);
    reg.emplace<PlayerComponent>(ent, playerId);
    reg.emplace<ChunkMemberComponent>(ent);
    reg.emplace<BoundingBoxComponent>(ent, PLAYER_BBOX_HX, PLAYER_BBOX_HY, PLAYER_BBOX_HZ);
    reg.emplace<PhysicsModeComponent>(ent, PhysicsMode::FULL);
}

} // namespace voxelmmo::PlayerEntity
