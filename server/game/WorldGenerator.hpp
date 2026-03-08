#pragma once
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include "common/MessageTypes.hpp"
#include "common/EntityType.hpp"
#include <entt/entt.hpp>
#include <vector>
#include <cstdint>
#include <functional>

namespace voxelmmo {

// Forward declarations to avoid circular includes
class ChunkRegistry;
class EntityFactory;

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
     * @brief Construct a lean world generator with the given seed and type.
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
    
    /** @return The test entity type (for TEST mode). */
    EntityType getTestEntityType() const noexcept { return testEntityType_; }
    
    /**
     * @brief Get the computed player spawn position.
     * @return Spawn position in sub-voxels as {x, y, z}.
     */
    const int32_t* getPlayerSpawnPos() const noexcept { return playerSpawnPos_; }
    
    /**
     * @brief Generate and activate initial chunks around a center position.
     *
     * Populates the chunkRegistry with chunks within radius of the center position.
     * Chunks are generated with voxels, activated, and entities are spawned.
     *
     * @param chunkRegistry        Registry to populate with generated chunks.
     * @param centerX/centerY/centerZ Center position in sub-voxels (typically 0,0,0).
     * @param initialActivationRadius Radius around center to generate chunks.
     * @param entityFactory        Factory to queue entity spawn requests.
     * @param tick                 Current server tick.
     */
    void generateChunks(ChunkRegistry& chunkRegistry,
                        int32_t centerX, int32_t centerY, int32_t centerZ,
                        int32_t initialActivationRadius,
                        EntityFactory& entityFactory,
                        uint32_t tick);
    
    /**
     * @brief Compute player spawn position at column (0,0).
     *
     * Finds the top solid voxel at world column (0,0) and sets spawn
     * 1 meter (SUBVOXEL_SIZE units) above it.
     */
    void computePlayerSpawnPos();
    
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
     * @brief Return the surface world-Y voxel at voxel column (voxelX, voxelZ).
     *
     * Matches the height used by generate(): result is in [4, 30].
     * The surface voxel (GRASS) occupies world-Y = getSurfaceY(voxelX, voxelZ);
     * the voxel above it is AIR.
     *
     * @param voxelX  World X coordinate in voxels (not sub-voxels).
     * @param voxelZ  World Z coordinate in voxels (not sub-voxels).
     */
    int32_t getSurfaceY(int voxelX, int voxelZ) const noexcept;

    /**
     * @brief Queue entity spawn requests for a newly activated chunk.
     *
     * Called after voxel generation. Queues passive mobs like sheep
     * on surface grass blocks into the entity factory (deferred creation).
     *
     * @param chunkId       Chunk being activated.
     * @param entityFactory Entity factory to queue spawn requests into.
     * @param tick          Current server tick (for state initialization).
     */
    void generateEntities(ChunkId chunkId, EntityFactory& entityFactory, uint32_t tick) const;
    
    /**
     * @brief Queue a player entity spawn at the player spawn position.
     *
     * Called when a player joins. Queues the player entity spawn into the
     * entity factory (deferred creation). The spawn position is the
     * pre-computed playerSpawnPos from computePlayerSpawnPos().
     *
     * Note: Chunk generation and SELF_ENTITY message are handled later
     * in the flow by GameEngine.
     *
     * @param entityFactory Entity factory to queue spawn requests into.
     * @param type          Entity type (PLAYER or GHOST_PLAYER).
     * @param playerId      Persistent player identifier.
     */
    void addPlayer(EntityFactory& entityFactory, EntityType type, PlayerId playerId) const;
    
private:
    uint32_t seed_;
    GeneratorType type_;
    EntityType testEntityType_;
    mutable bool testEntitySpawned_ = false;      ///< Track if test entity was spawned
    int32_t playerSpawnPos_[3] = {0, 0, 0};       ///< Player spawn (computed separately)
};

} // namespace voxelmmo
