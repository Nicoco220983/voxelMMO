#include "game/systems/PhysicsSystem.hpp"
#include "game/Chunk.hpp"
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
};

bool isSolid(VoxelType vt) { return vt != VoxelTypes::AIR; }

int32_t sweepY(AABB aabb, int32_t dy, VoxelContext& ctx) {
    if (dy == 0) return 0;

    const int32_t vxMin = aabb.minX >> SUBVOXEL_BITS;
    const int32_t vxMax = (aabb.maxX - 1) >> SUBVOXEL_BITS;
    const int32_t vzMin = aabb.minZ >> SUBVOXEL_BITS;
    const int32_t vzMax = (aabb.maxZ - 1) >> SUBVOXEL_BITS;

    if (dy < 0) {
        const int32_t vyFrom = (aabb.minY + dy) >> SUBVOXEL_BITS;
        const int32_t vyTo   = (aabb.minY - 1) >> SUBVOXEL_BITS;
        for (int32_t vy = vyFrom; vy <= vyTo; ++vy) {
            for (int32_t vx = vxMin; vx <= vxMax; ++vx) {
                for (int32_t vz = vzMin; vz <= vzMax; ++vz) {
                    if (isSolid(ctx.getAtVoxel(vx, vy, vz))) {
                        const int32_t push = (vy + 1) * SUBVOXEL_SIZE - aabb.minY;
                        if (push > dy) dy = push;
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

void apply(entt::registry& registry, const ChunkRegistry& chunkRegistry)
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

            const int32_t gravVy = std::max(dyn.vy - GRAVITY_DECREMENT, -TERMINAL_VELOCITY);

            const int32_t resolvedDy = Physics::sweepY(aabb, gravVy, ctx);
            const bool    grounded   = (gravVy < 0 && resolvedDy > gravVy);
            aabb.minY += resolvedDy; aabb.maxY += resolvedDy;

            const int32_t resolvedDx = Physics::sweepX(aabb, dyn.vx, ctx);
            aabb.minX += resolvedDx; aabb.maxX += resolvedDx;

            const int32_t resolvedDz = Physics::sweepZ(aabb, dyn.vz, ctx);

            const int32_t nvx = (resolvedDx == 0 && dyn.vx != 0) ? 0 : dyn.vx;
            const int32_t nvz = (resolvedDz == 0 && dyn.vz != 0) ? 0 : dyn.vz;
            const int32_t nvy = grounded ? 0 : resolvedDy;

            const bool collided = (grounded != dyn.grounded)
                               || (nvx != dyn.vx)
                               || (nvz != dyn.vz)
                               || (nvy < 0 && resolvedDy != gravVy);

            DynamicPositionComponent::modify(registry, ent,
                dyn.x + resolvedDx, dyn.y + resolvedDy, dyn.z + resolvedDz,
                nvx, nvy, nvz, grounded, collided);
        }
    }
}

} // namespace PhysicsSystem
} // namespace voxelmmo
