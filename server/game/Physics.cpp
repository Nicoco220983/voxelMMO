#include "game/Physics.hpp"
#include "common/VoxelTypes.hpp"
#include <algorithm>

namespace voxelmmo {
namespace Physics {

// ── VoxelContext ─────────────────────────────────────────────────────────────

VoxelType VoxelContext::getAt(int32_t wx, int32_t wy, int32_t wz) {
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

// ── Solid predicate ──────────────────────────────────────────────────────────

bool isSolid(VoxelType vt) {
    return vt != VoxelTypes::AIR;
}

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

int32_t sweepY(AABB aabb, int32_t dy, VoxelContext& ctx) {
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
} // namespace voxelmmo
