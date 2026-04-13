#include "game/voxels/GoblinBedVoxel.hpp"
#include "game/Chunk.hpp"
#include "game/WorldChunk.hpp"
#include "game/entities/EntityFactory.hpp"
#include "common/EntityType.hpp"

namespace voxelmmo {

namespace GoblinBedVoxel {

void onActivate(ChunkId chunkId,
                const Chunk& chunk,
                int x, int y, int z,
                EntityFactory& entityFactory,
                uint32_t tick) {
    // Check there's room above (air block)
    if (y + 1 >= CHUNK_SIZE_Y) return;
    if (chunk.world.getVoxel(x, y + 1, z) != VoxelTypes::AIR) return;

    // Get chunk base position in world coordinates
    const int32_t baseX = chunkId.x() * CHUNK_SIZE_X;
    const int32_t baseY = chunkId.y() * CHUNK_SIZE_Y;
    const int32_t baseZ = chunkId.z() * CHUNK_SIZE_Z;

    // Spawn goblin above the bed (centered in the voxel)
    int32_t spawnX = (baseX + x) * SUBVOXEL_SIZE + (SUBVOXEL_SIZE / 2);
    int32_t spawnY = (baseY + y + 1) * SUBVOXEL_SIZE;  // One block above
    int32_t spawnZ = (baseZ + z) * SUBVOXEL_SIZE + (SUBVOXEL_SIZE / 2);

    entityFactory.spawnAI(EntityType::GOBLIN, spawnX, spawnY, spawnZ, tick);
}

void clearSpawnTracking() {
    // No-op: no tracking state to clear
}

size_t getTrackedSpawnCount() {
    return 0;  // No tracking
}

} // namespace GoblinBedVoxel

// Static registration of GoblinBed voxel type
namespace {
[[maybe_unused]] const auto& _goblinBedReg = VoxelRegistrar<GoblinBedVoxelTag>{};
} // anonymous namespace

} // namespace voxelmmo
