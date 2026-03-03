#pragma once
#include "common/Types.hpp"
#include "game/Chunk.hpp"
#include <unordered_map>
#include <memory>

namespace voxelmmo {
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
    VoxelType getAt(int32_t wx, int32_t wy, int32_t wz);
};

/** @brief True iff vt is a solid (non-air) voxel type. */
bool isSolid(VoxelType vt);

/**
 * @brief Sweep AABB along the Y axis by dy sub-voxels and return the
 *        collision-clamped displacement (|result| ≤ |dy|, same sign as dy).
 */
int32_t sweepY(AABB aabb, int32_t dy, VoxelContext& ctx);

/**
 * @brief Sweep AABB along the X axis by dx sub-voxels and return the
 *        collision-clamped displacement.
 */
int32_t sweepX(AABB aabb, int32_t dx, VoxelContext& ctx);

/**
 * @brief Sweep AABB along the Z axis by dz sub-voxels and return the
 *        collision-clamped displacement.
 */
int32_t sweepZ(AABB aabb, int32_t dz, VoxelContext& ctx);

} // namespace Physics
} // namespace voxelmmo
