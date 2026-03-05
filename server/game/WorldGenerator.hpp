#pragma once
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include "common/MessageTypes.hpp"
#include <entt/entt.hpp>
#include <vector>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief World generator type.
 */
enum class GeneratorType : uint8_t {
    NORMAL = 0,  ///< Procedural terrain with noise
    TEST   = 1   ///< Test world: flat terrain + single configurable entity
};

/**
 * @brief Stateful procedural terrain generator.
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
 * The generator is deterministic for a given seed: same (cx, cy, cz) always
 * yields the same voxel data.
 */
class WorldGenerator {
public:
    /**
     * @brief Construct a world generator with the given seed and type.
     * @param seed           Random seed for deterministic generation.
     * @param type           Generator type (NORMAL or TEST).
     * @param testEntityType Entity type to spawn in TEST mode (default: SHEEP).
     */
    WorldGenerator(uint32_t seed = 0, GeneratorType type = GeneratorType::NORMAL,
                   EntityType testEntityType = EntityType::SHEEP);
    
    /** @return The seed used for generation. */
    uint32_t getSeed() const noexcept { return seed_; }
    
    /** @return The generator type. */
    GeneratorType getType() const noexcept { return type_; }
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
    
private:
    uint32_t seed_;
    GeneratorType type_;
    EntityType testEntityType_;
    mutable bool testEntitySpawned_ = false;  ///< Track if test entity was already spawned
};

} // namespace voxelmmo
