#include "game/systems/PhysicsSystem.hpp"
#include "game/Chunk.hpp"
#include "game/components/GroundContactComponent.hpp"
#include "game/components/JumpComponent.hpp"
#include "game/entities/PlayerEntity.hpp"
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
        // Use cached physic type array (fast O(1) access)
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

    // Track the surface type of the collision (for bounce effects)
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
                            // Record the surface we hit (for downward sweep, it's the top of this voxel)
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

void apply(entt::registry& registry, const ChunkRegistry& chunkRegistry, uint32_t currentTick)
{
    Physics::VoxelContext ctx{chunkRegistry};

    for (auto& [chunkId, chunkPtr] : chunkRegistry.getAllChunks()) {
        ctx.lastChunk   = chunkPtr.get();
        ctx.lastChunkId = chunkId;

        for (auto ent : chunkPtr->entities) {
            if (!registry.all_of<DynamicPositionComponent,
                                 BoundingBoxComponent,
                                 PhysicsModeComponent>(ent)) continue;

            const DynamicPositionComponent dyn  = registry.get<DynamicPositionComponent>(ent);
            const BoundingBoxComponent&    bbox = registry.get<BoundingBoxComponent>(ent);
            const PhysicsMode              mode = registry.get<PhysicsModeComponent>(ent).mode;

            if (mode == PhysicsMode::GHOST) {
                if (dyn.vx == 0 && dyn.vy == 0 && dyn.vz == 0) continue;
                DynamicPositionComponent::modify(registry, ent,
                    dyn.x + dyn.vx, dyn.y + dyn.vy, dyn.z + dyn.vz,
                    dyn.vx, dyn.vy, dyn.vz,
                    /*grounded=*/ true,
                    /*dirty=*/    false);
                continue;
            }

            Physics::AABB aabb{
                dyn.x - bbox.hx, dyn.y - bbox.hy, dyn.z - bbox.hz,
                dyn.x + bbox.hx, dyn.y + bbox.hy, dyn.z + bbox.hz
            };

            if (mode == PhysicsMode::FLYING) {
                if (dyn.vx == 0 && dyn.vy == 0 && dyn.vz == 0) continue;

                const int32_t resolvedDy = Physics::sweepY(aabb, dyn.vy, ctx);
                aabb.minY += resolvedDy; aabb.maxY += resolvedDy;
                const int32_t resolvedDx = Physics::sweepX(aabb, dyn.vx, ctx);
                aabb.minX += resolvedDx; aabb.maxX += resolvedDx;
                const int32_t resolvedDz = Physics::sweepZ(aabb, dyn.vz, ctx);

                const int32_t nvx = (resolvedDx == 0 && dyn.vx != 0) ? 0 : dyn.vx;
                const int32_t nvy = (resolvedDy == 0 && dyn.vy != 0) ? 0 : dyn.vy;
                const int32_t nvz = (resolvedDz == 0 && dyn.vz != 0) ? 0 : dyn.vz;
                const bool collided = (nvx != dyn.vx) || (nvy != dyn.vy) || (nvz != dyn.vz);

                DynamicPositionComponent::modify(registry, ent,
                    dyn.x + resolvedDx, dyn.y + resolvedDy, dyn.z + resolvedDz,
                    nvx, nvy, nvz,
                    /*grounded=*/ false, collided);
                continue;
            }

            // FULL physics mode (gravity + collision)
            const int32_t gravVy = std::max(dyn.vy - GRAVITY_DECREMENT, -TERMINAL_VELOCITY);

            // Sweep Y and capture the surface type we landed on
            VoxelPhysicType hitSurfaceType = VoxelPhysicTypes::AIR;
            const int32_t resolvedDy = Physics::sweepY(aabb, gravVy, ctx, &hitSurfaceType);
            
            const bool    grounded   = (gravVy < 0 && resolvedDy > gravVy);
            aabb.minY += resolvedDy; aabb.maxY += resolvedDy;

            const int32_t resolvedDx = Physics::sweepX(aabb, dyn.vx, ctx);
            aabb.minX += resolvedDx; aabb.maxX += resolvedDx;

            const int32_t resolvedDz = Physics::sweepZ(aabb, dyn.vz, ctx);

            // Track velocity modifications for surface effects
            int32_t nvx = (resolvedDx == 0 && dyn.vx != 0) ? 0 : dyn.vx;
            int32_t nvz = (resolvedDz == 0 && dyn.vz != 0) ? 0 : dyn.vz;
            int32_t nvy = grounded ? 0 : resolvedDy;

            // Detect landing and wall collisions
            const bool landed      = grounded && !dyn.grounded;
            const bool hitWallX    = (resolvedDx == 0 && dyn.vx != 0);
            const bool hitWallZ    = (resolvedDz == 0 && dyn.vz != 0);
            const bool hitCeiling  = (resolvedDy == 0 && gravVy > 0);
            
            const bool collided    = (grounded != dyn.grounded)
                                  || hitWallX || hitWallZ
                                  || (nvy < 0 && resolvedDy != gravVy);

            // Use the surface type from the sweep (the actual voxel we collided with)
            VoxelPhysicType groundType = VoxelPhysicTypes::AIR;
            if (grounded) {
                // If we landed, use the surface type from the sweep collision
                // This is more accurate than sampling center point (which might be over air)
                groundType = hitSurfaceType;
            }

            // Apply surface effects (bounce on slime, etc)
            if (grounded && groundType != VoxelPhysicTypes::AIR) {
                const auto& props = getVoxelPhysicProps(groundType);

                // SLIME: Bounce on landing (apply restitution)
                if (landed && props.restitution > 0) {
                    // Bounce based on impact velocity (gravVy), not start-of-tick velocity (dyn.vy)
                    int32_t impactVy = gravVy;
                    int32_t bounceVy = static_cast<int32_t>(-impactVy * static_cast<int32_t>(props.restitution) / 255);
                    
                    // Minimum bounce threshold
                    const int32_t threshold = (props.restitution >= 255) ? 1 : 10;
                    
                    // Delegate to JumpComponent for bounce + jump logic
                    auto* jump = registry.try_get<JumpComponent>(ent);
                    if (jump) {
                        nvy = jump->tryBounceJump(currentTick, bounceVy, PlayerEntity::PLAYER_JUMP_VY, threshold);
                    } else if (std::abs(bounceVy) >= threshold) {
                        nvy = bounceVy;
                    }
                }
            }

            // Auto-jump on non-bouncy surfaces when holding jump
            auto* jump = registry.try_get<JumpComponent>(ent);
            if (jump) {
                nvy = jump->tryAutoJump(currentTick, grounded, nvy, PlayerEntity::PLAYER_JUMP_VY);
            }

            // Update GroundContactComponent for InputSystem to read
            auto* contact = registry.try_get<GroundContactComponent>(ent);
            if (contact) {
                contact->groundType = groundType;
            } else if (grounded) {
                // Only add component if on ground (avoid bloat for airborne entities)
                registry.emplace<GroundContactComponent>(ent, groundType);
            }
            
            DynamicPositionComponent::modify(registry, ent,
                dyn.x + resolvedDx, dyn.y + resolvedDy, dyn.z + resolvedDz,
                nvx, nvy, nvz, grounded, collided);
        }
    }
}

} // namespace PhysicsSystem
} // namespace voxelmmo
