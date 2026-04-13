#pragma once
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include "common/VoxelCatalog.hpp"

namespace voxelmmo {

// Forward declarations
class Chunk;
class EntityFactory;

// Tag type for VoxelTraits specialization
struct GoblinBedVoxelTag {};

/**
 * @brief Goblin Bed voxel - spawns a goblin on chunk activation.
 *
 * Each GOBLIN_BED voxel spawns a goblin when its chunk is activated.
 * Simple implementation: no tracking, spawns on every activation.
 */
namespace GoblinBedVoxel {

/**
 * @brief Voxel activation callback - spawns a goblin above this GOBLIN_BED voxel.
 *
 * Called by WorldGenerator for each GOBLIN_BED voxel when its chunk is activated.
 *
 * @param chunkId The activated chunk ID
 * @param chunk The chunk data (read-only)
 * @param x Local X coordinate within chunk
 * @param y Local Y coordinate within chunk
 * @param z Local Z coordinate within chunk
 * @param entityFactory Factory for spawning entities
 * @param tick Current server tick
 */
void onActivate(ChunkId chunkId,
                const Chunk& chunk,
                int x, int y, int z,
                EntityFactory& entityFactory,
                uint32_t tick);

/**
 * @brief Clear all spawn tracking data (no-op for this implementation).
 */
void clearSpawnTracking();

/**
 * @brief Get the number of tracked spawns (always 0 for this implementation).
 */
size_t getTrackedSpawnCount();

} // namespace GoblinBedVoxel

// VoxelTraits specialization for GoblinBed
template<>
struct VoxelTraits<GoblinBedVoxelTag> {
    static constexpr VoxelType typeId = VoxelTypes::GOBLIN_BED;
    static constexpr std::string_view name = "GOBLIN_BED";
    static constexpr auto onActivate = GoblinBedVoxel::onActivate;
};

} // namespace voxelmmo
