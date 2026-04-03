#include "game/systems/PhysicsSystem.hpp"
#include "game/Chunk.hpp"
#include "game/components/GroundContactComponent.hpp"
#include "common/VoxelPhysicProps.hpp"
#include <algorithm>

namespace voxelmmo {
namespace Physics {

/** @brief Axis-aligned bounding box in sub-voxel coordinates. */
struct AABB {
    int32_t minX, minY, minZ;
    int32_t maxX, maxY, maxZ;
};

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
};

bool isSolid(VoxelType vt) { return vt != VoxelTypes::AIR; }

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
                    if (isSolid(ctx.getAtVoxel(vx, vy, vz))) {
                        const int32_t push = (vy + 1) * SUBVOXEL_SIZE - aabb.minY;
                        if (push > dy) {
                            dy = push;
                            hitSurfaceType = ctx.getPhysicTypeAtVoxel(vx, vy, vz);
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
                    if (isSolid(ctx.getAtVoxel(vx, vy, vz))) {
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
                    if (isSolid(ctx.getAtVoxel(vx, vy, vz))) {
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
                    if (isSolid(ctx.getAtVoxel(vx, vy, vz))) {
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
                    if (isSolid(ctx.getAtVoxel(vx, vy, vz))) {
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
                    if (isSolid(ctx.getAtVoxel(vx, vy, vz))) {
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
 */
void updateGroundContact(entt::registry& registry, entt::entity ent,
                         const CollisionState& collision,
                         VoxelPhysicType groundType,
                         int32_t bounceVelocity) {
    auto* contact = registry.try_get<GroundContactComponent>(ent);
    if (contact) {
        contact->groundType = groundType;
        contact->justLanded = collision.landed;
        contact->bounceVelocity = bounceVelocity;
    } else if (collision.grounded) {
        registry.emplace<GroundContactComponent>(ent, 
            GroundContactComponent{
                groundType,           // groundType
                VoxelPhysicTypes::AIR, // wallTypeX
                VoxelPhysicTypes::AIR, // wallTypeZ
                collision.landed,      // justLanded
                bounceVelocity         // bounceVelocity
            });
    }
}

/**
 * @brief Process FULL physics mode entity (gravity + collision + bounce).
 * 
 * Note: Jump handling is done by JumpSystem, which runs after PhysicsSystem.
 * PhysicsSystem only calculates physics and bounce; JumpSystem adds jump velocity.
 */
void processFullPhysicsEntity(entt::registry& registry, entt::entity ent,
                              const DynamicPositionComponent& dyn,
                              AABB aabb, VoxelContext& ctx) {
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
    
    if (collision.landed && groundType != VoxelPhysicTypes::AIR) {
        const auto& props = getVoxelPhysicProps(groundType);
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
    updateGroundContact(registry, ent, collision, groundType, bounceVelocity);

    // Apply final position and velocity
    DynamicPositionComponent::modify(registry, ent,
        dyn.x + resolvedDx, dyn.y + resolvedDy, dyn.z + resolvedDz,
        nvx, nvy, nvz, grounded, collision.collided);
}

/**
 * @brief Process a single entity based on its physics mode.
 */
void processEntity(entt::registry& registry, entt::entity ent,
                   const DynamicPositionComponent& dyn,
                   const BoundingBoxComponent& bbox,
                   PhysicsMode mode,
                   VoxelContext& ctx) {
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
            processFullPhysicsEntity(registry, ent, dyn, aabb, ctx);
            break;
        }
    }
}

void apply(entt::registry& registry, const ChunkRegistry& chunkRegistry) {
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

            processEntity(registry, ent, dyn, bbox, mode, ctx);
        }
    }
}

} // namespace PhysicsSystem
} // namespace voxelmmo
