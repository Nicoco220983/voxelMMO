#pragma once
#include "game/Chunk.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>
#include <algorithm>

namespace voxelmmo {

// ── Physics primitives ────────────────────────────────────────────────────────

namespace Physics {

/** @brief Axis-aligned bounding box in sub-voxel coordinates. */
struct AABB {
    int32_t minX, minY, minZ;
    int32_t maxX, maxY, maxZ;
};

/**
 * @brief Thin wrapper around the chunk map that caches the last-used chunk.
 *
 * Avoids repeated hash-map lookups when sweeping through consecutive voxels
 * that often fall in the same chunk as the moving entity.
 */
struct VoxelContext {
    const std::unordered_map<ChunkId, std::unique_ptr<Chunk>>& chunks;
    const Chunk* lastChunk   = nullptr;
    ChunkId      lastChunkId = {};

    /**
     * @brief Return the voxel type at sub-voxel world position (wx, wy, wz).
     *        Returns AIR for unloaded chunks (passable = safe outside loaded world).
     */
    VoxelType getAt(int32_t wx, int32_t wy, int32_t wz) {
        const ChunkId cid = chunkIdOf(wx, wy, wz);
        if (cid != lastChunkId) {
            const auto it = chunks.find(cid);
            lastChunkId   = cid;
            lastChunk     = (it != chunks.end()) ? it->second.get() : nullptr;
        }
        if (!lastChunk) return VoxelTypes::AIR;

        // Extract local voxel coords within the chunk using arithmetic right-shift
        // (C++20 guarantees two's complement; negative positions floor correctly).
        const uint8_t lx = static_cast<uint8_t>((wx >> SUBVOXEL_BITS) & CHUNK_MASK_X);
        const uint8_t ly = static_cast<uint8_t>((wy >> SUBVOXEL_BITS) & CHUNK_MASK_Y);
        const uint8_t lz = static_cast<uint8_t>((wz >> SUBVOXEL_BITS) & CHUNK_MASK_Z);
        return lastChunk->world.voxels[VoxelId::make(ly, lx, lz).packed];
    }
};

/** @brief True iff vt is a solid (non-air) voxel type. */
inline bool isSolid(VoxelType vt) { return vt != VoxelTypes::AIR; }

// ── Sweep helpers ─────────────────────────────────────────────────────────────
//
// Each sweep function checks only the voxels that the moving face would enter.
// Voxels already occupied by the AABB are never rechecked; this prevents
// false collisions when the entity is flush against a surface.
//
// Conventions:
//   - "voxel N" occupies sub-voxel range [N*SUBVOXEL_SIZE, (N+1)*SUBVOXEL_SIZE).
//   - The "face" voxel range for sweeping towards -X (minX face) covers:
//       [(minX + dx) >> BITS, (minX - 1) >> BITS]   (inclusive both ends).
//   - For the opposing dimension, the range covers the entire cross-section:
//       [minX >> BITS, (maxX - 1) >> BITS]

/**
 * @brief Sweep AABB along the Y axis by dy sub-voxels and return the
 *        collision-clamped displacement (|result| ≤ |dy|, same sign as dy).
 */
inline int32_t sweepY(AABB aabb, int32_t dy, VoxelContext& ctx) {
    if (dy == 0) return 0;

    // Cross-section on X and Z (same regardless of movement direction).
    const int32_t vxMin = aabb.minX >> SUBVOXEL_BITS;
    const int32_t vxMax = (aabb.maxX - 1) >> SUBVOXEL_BITS;
    const int32_t vzMin = aabb.minZ >> SUBVOXEL_BITS;
    const int32_t vzMax = (aabb.maxZ - 1) >> SUBVOXEL_BITS;

    if (dy < 0) {
        // Moving down: sweep the minY face.
        const int32_t vyFrom = (aabb.minY + dy) >> SUBVOXEL_BITS;   // target bottom voxel
        const int32_t vyTo   = (aabb.minY - 1) >> SUBVOXEL_BITS;    // current bottom voxel
        for (int32_t vy = vyFrom; vy <= vyTo; ++vy) {
            for (int32_t vx = vxMin; vx <= vxMax; ++vx) {
                for (int32_t vz = vzMin; vz <= vzMax; ++vz) {
                    if (isSolid(ctx.getAt(vx << SUBVOXEL_BITS, vy << SUBVOXEL_BITS, vz << SUBVOXEL_BITS))) {
                        // Push minY to rest on top of this voxel.
                        const int32_t push = (vy + 1) * SUBVOXEL_SIZE - aabb.minY;
                        if (push > dy) dy = push;  // dy is negative; max → less negative
                    }
                }
            }
        }
    } else {
        // Moving up: sweep the maxY face.
        const int32_t vyFrom = aabb.maxY >> SUBVOXEL_BITS;
        const int32_t vyTo   = (aabb.maxY + dy - 1) >> SUBVOXEL_BITS;
        for (int32_t vy = vyFrom; vy <= vyTo; ++vy) {
            for (int32_t vx = vxMin; vx <= vxMax; ++vx) {
                for (int32_t vz = vzMin; vz <= vzMax; ++vz) {
                    if (isSolid(ctx.getAt(vx << SUBVOXEL_BITS, vy << SUBVOXEL_BITS, vz << SUBVOXEL_BITS))) {
                        // Push maxY to just below the bottom of this voxel.
                        const int32_t push = vy * SUBVOXEL_SIZE - aabb.maxY;
                        if (push < dy) dy = push;  // dy is positive; min → less positive
                    }
                }
            }
        }
    }
    return dy;
}

/**
 * @brief Sweep AABB along the X axis by dx sub-voxels and return the
 *        collision-clamped displacement.
 */
inline int32_t sweepX(AABB aabb, int32_t dx, VoxelContext& ctx) {
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
                    if (isSolid(ctx.getAt(vx << SUBVOXEL_BITS, vy << SUBVOXEL_BITS, vz << SUBVOXEL_BITS))) {
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
                    if (isSolid(ctx.getAt(vx << SUBVOXEL_BITS, vy << SUBVOXEL_BITS, vz << SUBVOXEL_BITS))) {
                        const int32_t push = vx * SUBVOXEL_SIZE - aabb.maxX;
                        if (push < dx) dx = push;
                    }
                }
            }
        }
    }
    return dx;
}

/**
 * @brief Sweep AABB along the Z axis by dz sub-voxels and return the
 *        collision-clamped displacement.
 */
inline int32_t sweepZ(AABB aabb, int32_t dz, VoxelContext& ctx) {
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
                    if (isSolid(ctx.getAt(vx << SUBVOXEL_BITS, vy << SUBVOXEL_BITS, vz << SUBVOXEL_BITS))) {
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
                    if (isSolid(ctx.getAt(vx << SUBVOXEL_BITS, vy << SUBVOXEL_BITS, vz << SUBVOXEL_BITS))) {
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

// ── PhysicsSystem ─────────────────────────────────────────────────────────────

namespace PhysicsSystem {

/**
 * @brief Step physics for all entities with DynamicPositionComponent,
 *        BoundingBoxComponent, and PhysicsModeComponent.
 *
 * Iterates chunk-first so that the VoxelContext cache is pre-warmed with each
 * chunk, avoiding redundant hash-map lookups for entities near their chunk centre.
 */
inline void apply(entt::registry& registry,
                  std::unordered_map<ChunkId, std::unique_ptr<Chunk>>& chunks)
{
    Physics::VoxelContext ctx{chunks};

    // Chunk-first iteration: pre-warm the voxel cache with each chunk so that
    // entities near the chunk centre avoid a hash-map lookup per voxel.
    for (auto& [chunkId, chunkPtr] : chunks) {
        ctx.lastChunk   = chunkPtr.get();
        ctx.lastChunkId = chunkId;

        for (auto& [ent, _ceid] : chunkPtr->entities) {
            if (!registry.all_of<DynamicPositionComponent,
                                 BoundingBoxComponent,
                                 PhysicsModeComponent>(ent)) continue;

            // Snapshot current state (copies values before modify() overwrites them).
            const DynamicPositionComponent dyn  = registry.get<DynamicPositionComponent>(ent);
            const BoundingBoxComponent&    bbox = registry.get<BoundingBoxComponent>(ent);
            const PhysicsMode              mode = registry.get<PhysicsModeComponent>(ent).mode;

            // ── GHOST: no gravity, no collision ──────────────────────────────
            if (mode == PhysicsMode::GHOST) {
                if (dyn.vx == 0 && dyn.vy == 0 && dyn.vz == 0) continue;
                DynamicPositionComponent::modify(registry, ent,
                    dyn.x + dyn.vx, dyn.y + dyn.vy, dyn.z + dyn.vz,
                    dyn.vx, dyn.vy, dyn.vz,
                    /*grounded=*/ true,
                    /*dirty=*/    false);
                continue;
            }

            // ── Build AABB (shared by FLYING and FULL) ────────────────────────
            Physics::AABB aabb{
                dyn.x - bbox.hx, dyn.y - bbox.hy, dyn.z - bbox.hz,
                dyn.x + bbox.hx, dyn.y + bbox.hy, dyn.z + bbox.hz
            };

            // ── FLYING: no gravity, with collision ────────────────────────────
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

            // ── FULL: gravity + collision ─────────────────────────────────────
            // Gravity applied every tick unconditionally (no branch on grounded).
            // sweepY determines grounded output each tick — stable on flat ground,
            // naturally falls off ledges.
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
