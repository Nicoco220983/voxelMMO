#include "game/entities/PlayerEntity.hpp"
#include "game/entities/EntityFactory.hpp"
#include "game/components/JumpComponent.hpp"
#include "game/components/WalkComponent.hpp"
#include "game/components/HealthComponent.hpp"
#include "common/EntityCatalog.hpp"
#include <cmath>

namespace voxelmmo::PlayerEntity {

// Auto-registration in EntityCatalog
namespace {
    [[maybe_unused]] const EntityRegistrar<PlayerEntityTag>& _playerReg = 
        EntityRegistrar<PlayerEntityTag>{};
}

void computeVelocity(const InputComponent& inp, const DynamicPositionComponent& /*dyn*/, 
                     int32_t& nvx, int32_t& nvy, int32_t& nvz,
                     uint16_t maxSpeedXZ) {
    const uint8_t b = inp.buttons;
    const float cy = std::cos(inp.yaw), sy = std::sin(inp.yaw);
    float dx = 0, dz = 0;
    if (b & static_cast<uint8_t>(InputButton::FORWARD))  { dx += -sy; dz += -cy; }
    if (b & static_cast<uint8_t>(InputButton::BACKWARD)) { dx -= -sy; dz -= -cy; }
    if (b & static_cast<uint8_t>(InputButton::LEFT))     { dx -= cy;  dz += -sy; }
    if (b & static_cast<uint8_t>(InputButton::RIGHT))    { dx += cy;  dz += -sy; }
    
    const int32_t speedLimit = (maxSpeedXZ > 0) ? maxSpeedXZ : PLAYER_WALK_SPEED;
    
    const float hlen = std::sqrt(dx*dx + dz*dz);
    const float hs   = (hlen > 0.001f) ? (static_cast<float>(speedLimit) / hlen) : 0.0f;
    nvx = static_cast<int32_t>(dx * hs);
    nvz = static_cast<int32_t>(dz * hs);
    // nvy is not modified here - PhysicsSystem handles vertical velocity
    (void)nvy; // Suppress unused warning
}

entt::entity spawn(entt::registry& reg,
                   GlobalEntityId globalId,
                   SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z,
                   PlayerId playerId)
{
    // Base components (GlobalEntityId, Dirty, ChunkMembership, PendingCreate)
    // Chunk is computed from position inside BaseEntity::spawn()
    const entt::entity ent = BaseEntity::spawn(reg, globalId, x, y, z);

    // Player-specific components
    reg.emplace<DynamicPositionComponent>(ent, x, y, z, 0, 0, 0, /*grounded=*/false);
    reg.emplace<EntityTypeComponent>(ent, EntityType::PLAYER);
    reg.emplace<InputComponent>(ent);
    reg.emplace<PlayerComponent>(ent, playerId);
    reg.emplace<BoundingBoxComponent>(ent, PLAYER_BBOX_HX, PLAYER_BBOX_HY, PLAYER_BBOX_HZ);
    reg.emplace<PhysicsModeComponent>(ent, PhysicsMode::FULL);
    reg.emplace<JumpComponent>(ent);
    reg.emplace<WalkComponent>(ent);
    reg.emplace<HealthComponent>(ent, DEFAULT_HEALTH, DEFAULT_HEALTH, /*lastDamageTick=*/0);

    return ent;
}

entt::entity spawnImpl(entt::registry& reg,
                       GlobalEntityId globalId,
                       const EntitySpawnRequest& req)
{
    return spawn(reg, globalId, req.x, req.y, req.z, req.playerId);
}

size_t serializeCreate(entt::registry& reg, entt::entity ent, SafeBufWriter& w) {
    const size_t startOffset = w.offset();

    // For CREATE: component decides if it needs serialization based on non-default values
    uint8_t flags = 0;
    const auto& pos = reg.get<DynamicPositionComponent>(ent);
    if (pos.isNonDefault()) flags |= POSITION_BIT;

    const auto& health = reg.get<HealthComponent>(ent);
    if (health.isNonDefault()) flags |= HEALTH_BIT;

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::PLAYER));
    w.write(flags);

    // Serialize only if non-default
    if (flags & POSITION_BIT) {
        DynamicPositionComponent::serialize(reg, ent, POSITION_BIT, w);
    }
    if (flags & HEALTH_BIT) {
        HealthComponent::serialize(reg, ent, HEALTH_BIT, w);
    }

    return w.offset() - startOffset;
}

size_t serializeUpdate(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w) {
    const uint8_t flags = dirty.dirtyFlags & (POSITION_BIT | HEALTH_BIT);

    // Nothing to serialize if no tracked components are dirty
    if (flags == 0) {
        return 0;
    }

    const size_t startOffset = w.offset();

    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    w.write(gid.id);
    w.write(static_cast<uint8_t>(EntityType::PLAYER));
    w.write(flags);
    DynamicPositionComponent::serialize(reg, ent, flags, w);
    HealthComponent::serialize(reg, ent, flags, w);

    return w.offset() - startOffset;
}

} // namespace voxelmmo::PlayerEntity
