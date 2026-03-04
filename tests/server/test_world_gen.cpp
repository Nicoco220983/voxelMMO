#include <catch2/catch_test_macros.hpp>
#include "game/WorldChunk.hpp"
#include "game/WorldGenerator.hpp"
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"

using namespace voxelmmo;

// Helper: voxel index in row-major (y, x, z) layout
static size_t idx(int y, int x, int z) {
    return static_cast<size_t>(y) * CHUNK_SIZE_X * CHUNK_SIZE_Z
         + static_cast<size_t>(x) * CHUNK_SIZE_Z
         + static_cast<size_t>(z);
}

// Helper: generate chunk using WorldGenerator
static void generateChunk(WorldChunk& chunk, int cx, int cy, int cz) {
    WorldGenerator gen;
    gen.generate(chunk.voxels, cx, cy, cz);
}

// Surface is clamped to [4, 30].
// cy=-1  → worldY ∈ [-32, -1],  always below surface − 3 (4−3=1 > −1) → all STONE.
TEST_CASE("WorldGenerator - cy<0 layer is all stone", "[world_gen]") {
    WorldChunk chunk;
    generateChunk(chunk, 0, -1, 0);

    for (size_t i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
        REQUIRE(chunk.voxels[i] == VoxelTypes::STONE);
    }
}

// cy=1 → worldY ∈ [32, 63], always above max surface (30) → all AIR.
TEST_CASE("WorldGenerator - cy>=1 layer is all air", "[world_gen]") {
    WorldChunk chunk;
    generateChunk(chunk, 0, 1, 0);

    for (size_t i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
        REQUIRE(chunk.voxels[i] == VoxelTypes::AIR);
    }
}

// Wherever a GRASS voxel appears in a chunk, its neighbours must obey the
// layering rule: AIR above, up to 3 DIRT below, STONE deeper down.
// With 32-high chunks, the entire terrain (surface ∈ [4,30]) fits in cy=0.
TEST_CASE("WorldGenerator - voxel type layering is correct", "[world_gen]") {
    WorldChunk chunk;
    generateChunk(chunk, 3, 0, 5);  // cy=0 contains all terrain

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

    // At (cx=3, cz=5) the terrain is not all-mountain, so grass must exist.
    REQUIRE(found_grass);
}

TEST_CASE("WorldGenerator - deterministic output", "[world_gen]") {
    WorldChunk a, b;
    generateChunk(a, 7, 0, -3);
    generateChunk(b, 7, 0, -3);

    REQUIRE(a.voxels == b.voxels);
}

TEST_CASE("WorldGenerator - different positions produce different terrain", "[world_gen]") {
    WorldChunk a, b;
    generateChunk(a, 0, 0, 0);
    generateChunk(b, 100, 0, 100);

    REQUIRE(a.voxels != b.voxels);
}
