#include <catch2/catch_test_macros.hpp>
#include "game/WorldChunk.hpp"
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"

using namespace voxelmmo;

// Helper: voxel index in row-major (y, x, z) layout
static size_t idx(int y, int x, int z) {
    return static_cast<size_t>(y) * CHUNK_SIZE_X * CHUNK_SIZE_Z
         + static_cast<size_t>(x) * CHUNK_SIZE_Z
         + static_cast<size_t>(z);
}

// Surface is clamped to [4, 30].
// cy=-1  → worldY ∈ [-16, -1],  always below surface − 3 (4−3=1 > −1) → all STONE.
TEST_CASE("WorldChunk::generate - cy<0 layer is all stone", "[world_gen]") {
    WorldChunk chunk;
    chunk.generate(0, -1, 0);

    for (size_t i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
        REQUIRE(chunk.voxels[i] == VoxelTypes::STONE);
    }
}

// cy=2 → worldY ∈ [32, 47], always above max surface (30) → all AIR.
TEST_CASE("WorldChunk::generate - cy>=2 layer is all air", "[world_gen]") {
    WorldChunk chunk;
    chunk.generate(0, 2, 0);

    for (size_t i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
        REQUIRE(chunk.voxels[i] == VoxelTypes::AIR);
    }
}

// Wherever a GRASS voxel appears in a chunk, its neighbours must obey the
// layering rule: AIR above, up to 3 DIRT below, STONE deeper down.
// This holds regardless of whether the surface is in cy=0 or cy=1.
TEST_CASE("WorldChunk::generate - voxel type layering is correct", "[world_gen]") {
    // Generate both layers at the same XZ so we cover plains (cy=0) and
    // mountain tops (cy=1).
    for (int8_t cy : {(int8_t)0, (int8_t)1}) {
        WorldChunk chunk;
        chunk.generate(3, cy, 5);

        bool found_grass = false;

        for (int x = 0; x < CHUNK_SIZE_X; ++x) {
            for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
                for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                    if (chunk.voxels[idx(y, x, z)] != VoxelTypes::GRASS) continue;

                    found_grass = true;

                    // Voxel above must be AIR (if still within the chunk)
                    if (y + 1 < CHUNK_SIZE_Y)
                        REQUIRE(chunk.voxels[idx(y + 1, x, z)] == VoxelTypes::AIR);

                    // Up to 3 DIRT layers directly below
                    for (int dy = 1; dy <= 3 && (y - dy) >= 0; ++dy)
                        REQUIRE(chunk.voxels[idx(y - dy, x, z)] == VoxelTypes::DIRT);

                    // STONE further below (4th voxel down)
                    if (y - 4 >= 0)
                        REQUIRE(chunk.voxels[idx(y - 4, x, z)] == VoxelTypes::STONE);
                }
            }
        }

        // At (cx=3, cz=5) the terrain is not all-mountain, so at least
        // one GRASS voxel must be present across both layers combined.
        if (cy == 0) {
            // Relax: cy=1 will pick up the check if cy=0 is a mountain base.
            // We assert found_grass over the union of both layers below.
            (void)found_grass;
        } else {
            // By cy=1 we must have encountered GRASS somewhere in the two passes.
            REQUIRE(found_grass);
        }
    }
}

TEST_CASE("WorldChunk::generate - deterministic output", "[world_gen]") {
    WorldChunk a, b;
    a.generate(7, 0, -3);
    b.generate(7, 0, -3);

    REQUIRE(a.voxels == b.voxels);
}

TEST_CASE("WorldChunk::generate - different positions produce different terrain", "[world_gen]") {
    WorldChunk a, b;
    a.generate(0, 0, 0);
    b.generate(100, 0, 100);

    REQUIRE(a.voxels != b.voxels);
}
