#include "game/systems/PhysicsSystem.hpp"
#include "game/Chunk.hpp"
#include "game/components/GroundContactComponent.hpp"
#include "game/components/HealthComponent.hpp"
#include "common/VoxelPhysicProps.hpp"
#include <algorithm>

namespace voxelmmo {
namespace Physics {

/** @brief Axis-aligned bounding box in sub-voxel coordinates. */
struct AABB {
    int32_t minX, minY, minZ;
    int32_t maxX, maxY, maxZ;
};

inline bool isClimbable(VoxelPhysicType type) {
    return (getVoxelPhysicProps(type).flags & VoxelPhysicProps::FLAG_CLIMBABLE) != 0;
}

/**
 * @brief Thin wrapper around the chunk registry that caches the last-used chunk.
 */
struct VoxelContext {
    const ChunkRegistry& registry;
    const Chunk* lastChunk   = nullptr;
    ChunkId      lastChunkId = {};

    VoxelType getAtVoxel(int32_t vx, int32_t vy, int32_t vz) {
        const ChunkId cid = ChunkId::fromVoxelPos(vx, vy, vz);
        if (cid != lastChunkId) {
            lastChunkId = cid;
            lastChunk   = registry.getChunk(cid);
        }
        if (!lastChunk) return VoxelTypes::AIR;
        return lastChunk->world.getVoxel(vx, vy, vz);
    }

    /**
     * @brief Get cached physics type at voxel coordinates.
     * Direct array access to WorldChunk's cached voxelPhysicTypes (O(1)).
     */
    VoxelPhysicType getPhysicTypeAtVoxel(int32_t vx, int32_t vy, int32_t vz) {
        const ChunkId cid = ChunkId::fromVoxelPos(vx, vy, vz);
        if (cid != lastChunkId) {
            lastChunkId = cid;
            lastChunk   = registry.getChunk(cid);
        }
        if (!lastChunk) return VoxelPhysicTypes::AIR;
        const uint32_t localX = static_cast<uint32_t>(vx) & 0x1F;  // vx % 32
        const uint32_t localY = static_cast<uint32_t>(vy) & 0x1F;  // vy % 32
        const uint32_t localZ = static_cast<uint32_t>(vz) & 0x1F;  // vz % 32
        return lastChunk->world.getVoxelPhysicType(localX, localY, localZ);
    }

    /**
     * @brief Check if entity is touching a climbable voxel.
     * Requires: center X,Z within voxel's X,Z bounds (horizontally aligned)
     * AND: entity Y bounds overlap with voxel's Y bounds (vertically touching)
     */
    bool isTouchingClimbable(const AABB& aabb, int32_t centerX, int32_t centerY, int32_t centerZ) {
        // Check horizontal center alignment: entity center X,Z must be within voxel's X,Z
        const int32_t vx = centerX >> SUBVOXEL_BITS;
        const int32_t vz = centerZ >> SUBVOXEL_BITS;
        
        const int32_t voxelMinX = vx * SUBVOXEL_SIZE;
        const int32_t voxelMaxX = voxelMinX + SUBVOXEL_SIZE;
        const int32_t voxelMinZ = vz * SUBVOXEL_SIZE;
        const int32_t voxelMaxZ = voxelMinZ + SUBVOXEL_SIZE;
        
        // Center must be within voxel's X,Z bounds
        if (centerX < voxelMinX || centerX >= voxelMaxX) return false;
        if (centerZ < voxelMinZ || centerZ >= voxelMaxZ) return false;
        
        // Check vertical overlap: entity Y bounds must overlap with some voxel's Y bounds
        // Check all voxels that entity's Y range could touch
        const int32_t vyMin = aabb.minY >> SUBVOXEL_BITS;
        const int32_t vyMax = (aabb.maxY - 1) >> SUBVOXEL_BITS;
        
        for (int32_t vy = vyMin; vy <= vyMax; ++vy) {
            if (isClimbable(getPhysicTypeAtVoxel(vx, vy, vz))) {
                // Entity overlaps with this climbable voxel on Y axis
                const int32_t voxelMinY = vy * SUBVOXEL_SIZE;
                const int32_t voxelMaxY = voxelMinY + SUBVOXEL_SIZE;
                if (aabb.maxY > voxelMinY && aabb.minY < voxelMaxY) {
                    return true;
                }
            }
        }
        return false;
    }
};

bool isSolid(VoxelType vt) { return vt != VoxelTypes::AIR; }

/**
 * @brief Check if a voxel physics type is solid (has FLAG_SOLID).
 */
inline bool isPhysicTypeSolid(VoxelPhysicType pt) {
    return (getVoxelPhysicProps(pt).flags & VoxelPhysicProps::FLAG_SOLID) != 0;
}

/**
 * @brief Sweep AABB along Y axis and return resolved delta.
 * Also optionally returns the physics type of the surface that was hit (for landing detection).
 */
int32_t sweepY(AABB aabb, int32_t dy, VoxelContext& ctx, VoxelPhysicType* outHitSurfaceType = nullptr) {
    if (dy == 0) return 0;

    const int32_t vxMin = aabb.minX >> SUBVOXEL_BITS;
    const int32_t vxMax = (aabb.maxX - 1) >> SUBVOXEL_BITS;
    const int32_t vzMin = aabb.minZ >> SUBVOXEL_BITS;
    const int32_t vzMax = (aabb.maxZ - 1) >> SUBVOXEL_BITS;

    VoxelPhysicType hitSurfaceType = VoxelPhysicTypes::AIR;

    if (dy < 0) {
        const int32_t vyFrom = (aabb.minY + dy) >> SUBVOXEL_BITS;
        const int32_t vyTo   = (aabb.minY - 1) >> SUBVOXEL_BITS;
        for (int32_t vy = vyFrom; vy <= vyTo; ++vy) {
            for (int32_t vx = vxMin; vx <= vxMax; ++vx) {
                for (int32_t vz = vzMin; vz <= vzMax; ++vz) {
                    const VoxelPhysicType pt = ctx.getPhysicTypeAtVoxel(vx, vy, vz);
                    if (isPhysicTypeSolid(pt)) {
                        const int32_t push = (vy + 1) * SUBVOXEL_SIZE - aabb.minY;
                        if (push > dy) {
                            dy = push;
                            hitSurfaceType = pt;
                        }
                    }
                }
            }
        }
    } else {
        const int32_t vyFrom = aabb.maxY >> SUBVOXEL_BITS;
        const int32_t vyTo   = (aabb.maxY + dy - 1) >> SUBVOXEL_BITS;
        for (int32_t vy = vyFrom; vy <= vyTo; ++vy) {
            for (int32_t vx = vxMin; vx <= vxMax; ++vx) {
                for (int32_t vz = vzMin; vz <= vzMax; ++vz) {
                    if (isPhysicTypeSolid(ctx.getPhysicTypeAtVoxel(vx, vy, vz))) {
                        const int32_t push = vy * SUBVOXEL_SIZE - aabb.maxY;
                        if (push < dy) dy = push;
                    }
                }
            }
        }
    }
    
    if (outHitSurfaceType) {
        *outHitSurfaceType = hitSurfaceType;
    }
    return dy;
}

int32_t sweepX(AABB aabb, int32_t dx, VoxelContext& ctx) {
    if (dx == 0) return 0;

    const int32_t vyMin = aabb.minY >> SUBVOXEL_BITS;
    const int32_t vyMax = (aabb.maxY - 1) >> SUBVOXEL_BITS;
    const int32_t vzMin = aabb.minZ >> SUBVOXEL_BITS;
    const int32_t vzMax = (aabb.maxZ - 1) >> SUBVOXEL_BITS;

    if (dx < 0) {
        const int32_t vxFrom = (aabb.minX + dx) >> SUBVOXEL_BITS;
        const int32_t vxTo   = (aabb.minX - 1) >> SUBVOXEL_BITS;
        for (int32_t vx = vxFrom; vx <= vxTo; ++vx) {
            for (int32_t vy = vyMin; vy <= vyMax; ++vy) {
                for (int32_t vz = vzMin; vz <= vzMax; ++vz) {
                    if (isPhysicTypeSolid(ctx.getPhysicTypeAtVoxel(vx, vy, vz))) {
                        const int32_t push = (vx + 1) * SUBVOXEL_SIZE - aabb.minX;
                        if (push > dx) dx = push;
                    }
                }
            }
        }
    } else {
        const int32_t vxFrom = aabb.maxX >> SUBVOXEL_BITS;
        const int32_t vxTo   = (aabb.maxX + dx - 1) >> SUBVOXEL_BITS;
        for (int32_t vx = vxFrom; vx <= vxTo; ++vx) {
            for (int32_t vy = vyMin; vy <= vyMax; ++vy) {
                for (int32_t vz = vzMin; vz <= vzMax; ++vz) {
                    if (isPhysicTypeSolid(ctx.getPhysicTypeAtVoxel(vx, vy, vz))) {
                        const int32_t push = vx * SUBVOXEL_SIZE - aabb.maxX;
                        if (push < dx) dx = push;
                    }
                }
            }
        }
    }
    return dx;
}

int32_t sweepZ(AABB aabb, int32_t dz, VoxelContext& ctx) {
    if (dz == 0) return 0;

    const int32_t vyMin = aabb.minY >> SUBVOXEL_BITS;
    const int32_t vyMax = (aabb.maxY - 1) >> SUBVOXEL_BITS;
    const int32_t vxMin = aabb.minX >> SUBVOXEL_BITS;
    const int32_t vxMax = (aabb.maxX - 1) >> SUBVOXEL_BITS;

    if (dz < 0) {
        const int32_t vzFrom = (aabb.minZ + dz) >> SUBVOXEL_BITS;
        const int32_t vzTo   = (aabb.minZ - 1) >> SUBVOXEL_BITS;
        for (int32_t vz = vzFrom; vz <= vzTo; ++vz) {
            for (int32_t vy = vyMin; vy <= vyMax; ++vy) {
                for (int32_t vx = vxMin; vx <= vxMax; ++vx) {
                    if (isPhysicTypeSolid(ctx.getPhysicTypeAtVoxel(vx, vy, vz))) {
                        const int32_t push = (vz + 1) * SUBVOXEL_SIZE - aabb.minZ;
                        if (push > dz) dz = push;
                    }
                }
            }
        }
    } else {
        const int32_t vzFrom = aabb.maxZ >> SUBVOXEL_BITS;
        const int32_t vzTo   = (aabb.maxZ + dz - 1) >> SUBVOXEL_BITS;
        for (int32_t vz = vzFrom; vz <= vzTo; ++vz) {
            for (int32_t vy = vyMin; vy <= vyMax; ++vy) {
                for (int32_t vx = vxMin; vx <= vxMax; ++vx) {
                    if (isPhysicTypeSolid(ctx.getPhysicTypeAtVoxel(vx, vy, vz))) {
                        const int32_t push = vz * SUBVOXEL_SIZE - aabb.maxZ;
                        if (push < dz) dz = push;
                    }
                }
            }
        }
    }
    return dz;
}

} // namespace Physics

namespace PhysicsSystem {

using namespace Physics;

// ── Fall Damage Constants ───────────────────────────────────────────────────

/** @brief Minimum impact velocity (sub-voxels/tick) to take fall damage. 
 *  200 ≈ 3.1 voxels/s fall speed, roughly 0.5 voxel drop */
inline constexpr int32_t FALL_DAMAGE_MIN_IMPACT_VY = 100;

/** @brief Impact velocity that causes instant death. 
 *  900 ≈ 14 voxels/s, roughly 10 voxel drop */
inline constexpr int32_t FALL_DAMAGE_FATAL_IMPACT_VY = 500;

/** @brief Calculate fall damage from impact velocity.
 *  Returns damage amount (0 if below threshold).
 */
inline uint16_t calculateFallDamage(int32_t impactVy) {
    // impactVy is negative when falling, take absolute value
    const int32_t speed = -impactVy;  // Make positive
    
    if (speed <= FALL_DAMAGE_MIN_IMPACT_VY) {
        return 0;  // Safe landing
    }
    
    if (speed >= FALL_DAMAGE_FATAL_IMPACT_VY) {
        return 1000;  // Instant death (exceeds any reasonable max health)
    }
    
    // Linear damage scaling: maps [200, 900] to [1, 100]
    // Formula: damage = (speed - min) * 100 / (fatal - min) + 1
    const uint16_t damage = static_cast<uint16_t>(
        (speed - FALL_DAMAGE_MIN_IMPACT_VY) * 100 / 
        (FALL_DAMAGE_FATAL_IMPACT_VY - FALL_DAMAGE_MIN_IMPACT_VY) + 1
    );
    
    return damage;
}

/**
 * @brief Apply fall damage to entity on landing.
 * HealthComponent::applyDamage handles marking entity for deletion if it dies.
 * @return true if entity died from fall damage
 */
bool applyFallDamageOnLanding(entt::registry& registry, entt::entity ent,
                              int32_t impactVy, VoxelPhysicType groundType,
                              uint32_t tickCount) {
    const auto& props = getVoxelPhysicProps(groundType);
    
    // Only apply damage on solid surfaces
    if (!(props.flags & VoxelPhysicProps::FLAG_SOLID)) {
        return false;
    }
    
    auto* health = registry.try_get<HealthComponent>(ent);
    if (!health) {
        return false;
    }
    
    const uint16_t damage = calculateFallDamage(impactVy);
    if (damage == 0) {
        return false;
    }
    
    // HealthComponent::applyDamage handles deletion scheduling
    return HealthComponent::applyDamage(registry, ent, damage, tickCount);
}

/**
 * @brief Build AABB from position and bounding box.
 */
inline AABB buildAABB(const DynamicPositionComponent& dyn, const BoundingBoxComponent& bbox) {
    return AABB{
        dyn.x - bbox.hx, dyn.y - bbox.hy, dyn.z - bbox.hz,
        dyn.x + bbox.hx, dyn.y + bbox.hy, dyn.z + bbox.hz
    };
}

/**
 * @brief Stop velocity on axes where collision occurred.
 */
inline void resolveVelocityAfterCollision(int32_t& nvx, int32_t& nvy, int32_t& nvz,
                                          const DynamicPositionComponent& dyn,
                                          int32_t resolvedDx, int32_t resolvedDy, int32_t resolvedDz) {
    nvx = (resolvedDx == 0 && dyn.vx != 0) ? 0 : dyn.vx;
    nvy = (resolvedDy == 0 && dyn.vy != 0) ? 0 : dyn.vy;
    nvz = (resolvedDz == 0 && dyn.vz != 0) ? 0 : dyn.vz;
}

/**
 * @brief Process GHOST mode entity (velocity-only, no collision).
 */
void processGhostEntity(entt::registry& registry, entt::entity ent,
                        const DynamicPositionComponent& dyn) {
    if (dyn.vx == 0 && dyn.vy == 0 && dyn.vz == 0) return;
    
    DynamicPositionComponent::modify(registry, ent,
        dyn.x + dyn.vx, dyn.y + dyn.vy, dyn.z + dyn.vz,
        dyn.vx, dyn.vy, dyn.vz,
        /*grounded=*/ true,
        /*dirty=*/    false);
}

/**
 * @brief Process FLYING mode entity (collision but no gravity).
 */
void processFlyingEntity(entt::registry& registry, entt::entity ent,
                         const DynamicPositionComponent& dyn,
                         AABB aabb, VoxelContext& ctx) {
    if (dyn.vx == 0 && dyn.vy == 0 && dyn.vz == 0) return;

    const int32_t resolvedDy = sweepY(aabb, dyn.vy, ctx);
    aabb.minY += resolvedDy; aabb.maxY += resolvedDy;
    
    const int32_t resolvedDx = sweepX(aabb, dyn.vx, ctx);
    aabb.minX += resolvedDx; aabb.maxX += resolvedDx;
    
    const int32_t resolvedDz = sweepZ(aabb, dyn.vz, ctx);

    int32_t nvx, nvy, nvz;
    resolveVelocityAfterCollision(nvx, nvy, nvz, dyn, resolvedDx, resolvedDy, resolvedDz);
    const bool collided = (nvx != dyn.vx) || (nvy != dyn.vy) || (nvz != dyn.vz);

    DynamicPositionComponent::modify(registry, ent,
        dyn.x + resolvedDx, dyn.y + resolvedDy, dyn.z + resolvedDz,
        nvx, nvy, nvz,
        /*grounded=*/ false, collided);
}

/**
 * @brief Detect collision states (landing, wall hits, ceiling).
 */
struct CollisionState {
    bool landed;
    bool hitWallX;
    bool hitWallZ;
    bool hitCeiling;
    bool collided;
    bool grounded;
    
    static CollisionState detect(const DynamicPositionComponent& dyn,
                                 int32_t gravVy,
                                 int32_t resolvedDx, int32_t resolvedDy, int32_t resolvedDz) {
        CollisionState state;
        state.grounded   = (gravVy < 0 && resolvedDy > gravVy);
        state.landed     = state.grounded && !dyn.grounded;
        state.hitWallX   = (resolvedDx == 0 && dyn.vx != 0);
        state.hitWallZ   = (resolvedDz == 0 && dyn.vz != 0);
        state.hitCeiling = (resolvedDy == 0 && gravVy > 0);
        state.collided   = (state.grounded != dyn.grounded)
                        || state.hitWallX || state.hitWallZ
                        || (state.grounded && resolvedDy != gravVy);
        return state;
    }
};

/**
 * @brief Calculate bounce velocity from surface restitution.
 * @return Bounce velocity, or 0 if below threshold.
 */
int32_t calculateBounceVelocity(int32_t impactVy, uint8_t restitution) {
    int32_t bounceVy = static_cast<int32_t>(-impactVy * static_cast<int32_t>(restitution) / 255);
    
    const int32_t threshold = (restitution >= 255) ? 1 : 10;
    if (std::abs(bounceVy) >= threshold) {
        return bounceVy;
    }
    return 0;
}

/**
 * @brief Update GroundContactComponent with current ground state.
 * 
 * PhysicsSystem writes ground state here; JumpSystem reads it to decide jumps.
 * This separates physics simulation from jump game logic.
 * 
 * @param isClimbing When true, entity center is in climbable voxel (ladder)
 */
void updateGroundContact(entt::registry& registry, entt::entity ent,
                         const CollisionState& collision,
                         VoxelPhysicType groundType,
                         int32_t bounceVelocity,
                         bool isClimbing = false) {
    auto* contact = registry.try_get<GroundContactComponent>(ent);
    if (contact) {
        contact->groundType = groundType;
        contact->justLanded = collision.landed;
        contact->bounceVelocity = bounceVelocity;
        contact->isClimbing = isClimbing;
    } else if (collision.grounded || isClimbing) {
        registry.emplace<GroundContactComponent>(ent, 
            GroundContactComponent{
                groundType,            // groundType
                VoxelPhysicTypes::AIR, // wallTypeX
                VoxelPhysicTypes::AIR, // wallTypeZ
                collision.landed,      // justLanded
                bounceVelocity,        // bounceVelocity
                isClimbing             // isClimbing
            });
    }
}

/**
 * @brief Process entity climbing a ladder (center inside climbable voxel).
 * No gravity, collision enabled, grounded=false to allow vertical input.
 */
void processClimbingEntity(entt::registry& registry, entt::entity ent,
                           const DynamicPositionComponent& dyn,
                           AABB aabb, VoxelContext& ctx) {
    // No gravity when climbing - velocity preserved for input-controlled movement
    // Process collision on all axes like flying mode
    const int32_t resolvedDy = sweepY(aabb, dyn.vy, ctx);
    aabb.minY += resolvedDy; aabb.maxY += resolvedDy;
    
    const int32_t resolvedDx = sweepX(aabb, dyn.vx, ctx);
    aabb.minX += resolvedDx; aabb.maxX += resolvedDx;
    
    const int32_t resolvedDz = sweepZ(aabb, dyn.vz, ctx);

    int32_t nvx, nvy, nvz;
    resolveVelocityAfterCollision(nvx, nvy, nvz, dyn, resolvedDx, resolvedDy, resolvedDz);
    const bool collided = (nvx != dyn.vx) || (nvy != dyn.vy) || (nvz != dyn.vz);

    // Update ground contact to indicate climbing state
    if (auto* contact = registry.try_get<GroundContactComponent>(ent)) {
        contact->isClimbing = true;
    } else {
        registry.emplace<GroundContactComponent>(ent, 
            GroundContactComponent{
                VoxelPhysicTypes::AIR,  // groundType
                VoxelPhysicTypes::AIR,  // wallTypeX
                VoxelPhysicTypes::AIR,  // wallTypeZ
                false,                  // justLanded
                0,                      // bounceVelocity
                true                    // isClimbing
            });
    }

    // grounded=true tells client not to apply gravity (we're climbing, not falling)
    // InputSystem uses isClimbing flag to handle climb up/down with JUMP/DESCEND
    DynamicPositionComponent::modify(registry, ent,
        dyn.x + resolvedDx, dyn.y + resolvedDy, dyn.z + resolvedDz,
        nvx, nvy, nvz,
        /*grounded=*/ true, collided);
}

/**
 * @brief Process FULL physics mode entity (gravity + collision + bounce).
 * 
 * Note: Jump handling is done by JumpSystem, which runs after PhysicsSystem.
 * PhysicsSystem only calculates physics and bounce; JumpSystem adds jump velocity.
 */
void processFullPhysicsEntity(entt::registry& registry, entt::entity ent,
                              const DynamicPositionComponent& dyn,
                              AABB aabb, VoxelContext& ctx, uint32_t tickCount) {
    // Check if entity is touching a climbable voxel (ladder)
    // Requires: center X,Z within voxel bounds, AND Y overlap with climbable voxel
    if (ctx.isTouchingClimbable(aabb, dyn.x, dyn.y, dyn.z)) {
        processClimbingEntity(registry, ent, dyn, aabb, ctx);
        return;
    }

    // Apply gravity
    const int32_t gravVy = std::max(dyn.vy - GRAVITY_DECREMENT, -TERMINAL_VELOCITY);

    // Sweep all axes
    VoxelPhysicType hitSurfaceType = VoxelPhysicTypes::AIR;
    const int32_t resolvedDy = sweepY(aabb, gravVy, ctx, &hitSurfaceType);
    const bool grounded = (gravVy < 0 && resolvedDy > gravVy);
    aabb.minY += resolvedDy; aabb.maxY += resolvedDy;

    const int32_t resolvedDx = sweepX(aabb, dyn.vx, ctx);
    aabb.minX += resolvedDx; aabb.maxX += resolvedDx;

    const int32_t resolvedDz = sweepZ(aabb, dyn.vz, ctx);

    // Detect collision states
    const auto collision = CollisionState::detect(dyn, gravVy, resolvedDx, resolvedDy, resolvedDz);

    // Determine ground type and calculate bounce
    VoxelPhysicType groundType = grounded ? hitSurfaceType : VoxelPhysicTypes::AIR;
    int32_t bounceVelocity = 0;
    
    // Apply fall damage on hard landing (before bounce calculation)
    if (collision.landed && groundType != VoxelPhysicTypes::AIR) {
        const auto& props = getVoxelPhysicProps(groundType);
        
        // Apply fall damage (HealthComponent handles death marking)
        if (props.flags & VoxelPhysicProps::FLAG_SOLID) {
            applyFallDamageOnLanding(registry, ent, gravVy, groundType, tickCount);
        }
        
        if (props.restitution > 0) {
            bounceVelocity = calculateBounceVelocity(gravVy, props.restitution);
        }
    }

    // Calculate new velocities (horizontal)
    int32_t nvx = (resolvedDx == 0 && dyn.vx != 0) ? 0 : dyn.vx;
    int32_t nvz = (resolvedDz == 0 && dyn.vz != 0) ? 0 : dyn.vz;
    
    // Vertical velocity: bounce if landed on bouncy surface, otherwise stop if grounded
    int32_t nvy = grounded ? bounceVelocity : resolvedDy;

    // Update ground contact component for JumpSystem to read
    updateGroundContact(registry, ent, collision, groundType, bounceVelocity, /*isClimbing=*/false);

    // Apply final position and velocity
    // Only mark dirty if something unpredictable happened (state change, wall hit, bounce)
    // Constant-velocity movement is predictable by client, don't mark dirty for small movements
    const bool dirty = (grounded != dyn.grounded) || collision.hitWallX || collision.hitWallZ || 
                       collision.hitCeiling || bounceVelocity != 0;
    DynamicPositionComponent::modify(registry, ent,
        dyn.x + resolvedDx, dyn.y + resolvedDy, dyn.z + resolvedDz,
        nvx, nvy, nvz, grounded, dirty);
}

/**
 * @brief Process a single entity based on its physics mode.
 */
void processEntity(entt::registry& registry, entt::entity ent,
                   const DynamicPositionComponent& dyn,
                   const BoundingBoxComponent& bbox,
                   PhysicsMode mode,
                   VoxelContext& ctx, uint32_t tickCount) {
    switch (mode) {
        case PhysicsMode::GHOST:
            processGhostEntity(registry, ent, dyn);
            break;
            
        case PhysicsMode::FLYING: {
            AABB aabb = buildAABB(dyn, bbox);
            processFlyingEntity(registry, ent, dyn, aabb, ctx);
            break;
        }
            
        case PhysicsMode::FULL: {
            AABB aabb = buildAABB(dyn, bbox);
            processFullPhysicsEntity(registry, ent, dyn, aabb, ctx, tickCount);
            break;
        }
    }
}

void apply(entt::registry& registry, const ChunkRegistry& chunkRegistry, uint32_t tickCount) {
    VoxelContext ctx{chunkRegistry};

    for (auto& [chunkId, chunkPtr] : chunkRegistry.getAllChunks()) {
        ctx.lastChunk   = chunkPtr.get();
        ctx.lastChunkId = chunkId;

        for (auto ent : chunkPtr->entities) {
            
            if (!registry.all_of<DynamicPositionComponent,
                                 BoundingBoxComponent,
                                 PhysicsModeComponent>(ent)) {
                continue;
            }

            const auto& dyn  = registry.get<DynamicPositionComponent>(ent);
            const auto& bbox = registry.get<BoundingBoxComponent>(ent);
            const auto mode  = registry.get<PhysicsModeComponent>(ent).mode;

            processEntity(registry, ent, dyn, bbox, mode, ctx, tickCount);
        }
    }
}

} // namespace PhysicsSystem
} // namespace voxelmmo
