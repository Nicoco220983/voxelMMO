#pragma once
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include <entt/entt.hpp>
#include <vector>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Stateless procedural terrain generator.
 *
 * Fills a flat voxel buffer (row-major y, x, z) using a multi-frequency
 * 2D simplex noise heightmap:
 *
 *   - fBm base (3 octaves, periods 128/64/32):  rolling hills & valleys
 *   - Ridged noise (period 256, cubed):          sparse tall mountain peaks
 *   - Surface world-Y clamped to [4, 30]
 *
 * Performance: noise is evaluated on a 4-voxel grid (17×17 samples per
 * 64×64 chunk) and bilinearly interpolated, reducing noise calls ~14×.
 *
 * Guaranteed layer properties:
 *   cy ≤ -1  (worldY ≤ -1)   → all STONE  (surface ≥ 4)
 *   cy ≥  2  (worldY ≥ 32)   → all AIR    (surface ≤ 30)
 *
 * The generator is deterministic: same (cx, cy, cz) always yields the
 * same voxel data.
 */
class WorldGenerator {
public:
    /**
     * @brief Fill @p voxels with terrain data for chunk (cx, cy, cz).
     *
     * @p voxels must already be sized to CHUNK_VOXEL_COUNT.
     *
     * @param voxels  Output buffer — overwritten entirely.
     * @param cx      Chunk X coordinate.
     * @param cy      Chunk Y coordinate (vertical layer).
     * @param cz      Chunk Z coordinate.
     */
    void generate(std::vector<VoxelType>& voxels,
                  int32_t chunkX, int32_t chunkY, int32_t chunkZ) const;

    /**
     * @brief Return the surface world-Y voxel at world position (wx, wz).
     *
     * Matches the height used by generate(): result is in [4, 30].
     * The surface voxel (GRASS) occupies world-Y = surfaceY(wx, wz);
     * the voxel above it is AIR.
     */
    int32_t surfaceY(float wx, float wz) const noexcept;

    /**
     * @brief Spawn entities for a newly activated chunk.
     *
     * Called after voxel generation. Spawns passive mobs like sheep
     * on surface grass blocks.
     *
     * @param chunkId    Chunk being activated.
     * @param registry   Entity registry to create entities in.
     * @param tick       Current server tick (for state initialization).
     */
    void generateEntities(ChunkId chunkId, entt::registry& registry, uint32_t tick) const;
};

} // namespace voxelmmo
