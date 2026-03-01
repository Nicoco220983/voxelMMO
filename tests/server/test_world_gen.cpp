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

TEST_CASE("WorldChunk::generate - cy<0 layer is all stone", "[world_gen]") {
    WorldChunk chunk;
    chunk.generate(0, -1, 0);

    // cy=-1 → worldY ∈ [-16, -1], all below any possible surface (min surfaceY = 4)
    for (size_t i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
        REQUIRE(chunk.voxels[i] == VoxelTypes::STONE);
    }
}

TEST_CASE("WorldChunk::generate - cy>1 layer is all air", "[world_gen]") {
    WorldChunk chunk;
    chunk.generate(0, 2, 0);

    // cy=2 → worldY ∈ [32, 47], all above any possible surface (max surfaceY = 13)
    for (size_t i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
        REQUIRE(chunk.voxels[i] == VoxelTypes::AIR);
    }
}

TEST_CASE("WorldChunk::generate - cy=0 surface in [4,13] with correct layers", "[world_gen]") {
    WorldChunk chunk;
    chunk.generate(0, 0, 0);

    bool found_grass = false;

    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            // Find topmost non-air voxel in this column
            int surface_y = -1;
            for (int y = CHUNK_SIZE_Y - 1; y >= 0; --y) {
                if (chunk.voxels[idx(y, x, z)] != VoxelTypes::AIR) {
                    surface_y = y;
                    break;
                }
            }

            // Surface must exist and be in [4, 13]
            REQUIRE(surface_y >= 4);
            REQUIRE(surface_y <= 13);

            // Grass at the surface
            REQUIRE(chunk.voxels[idx(surface_y, x, z)] == VoxelTypes::GRASS);
            found_grass = true;

            // Up to 3 dirt layers below grass
            for (int dy = 1; dy <= 3 && (surface_y - dy) >= 0; ++dy) {
                REQUIRE(chunk.voxels[idx(surface_y - dy, x, z)] == VoxelTypes::DIRT);
            }

            // Stone further below (if the column is tall enough)
            if (surface_y - 4 >= 0) {
                REQUIRE(chunk.voxels[idx(surface_y - 4, x, z)] == VoxelTypes::STONE);
            }
        }
    }

    REQUIRE(found_grass);
}

TEST_CASE("WorldChunk::generate - cy=1 is all air (surface never reaches second layer)", "[world_gen]") {
    WorldChunk chunk;
    chunk.generate(0, 1, 0);

    // cy=1 → worldY ∈ [16, 31], all above max surfaceY (13)
    for (size_t i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
        REQUIRE(chunk.voxels[i] == VoxelTypes::AIR);
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

    // With high probability, two distant chunks differ
    REQUIRE(a.voxels != b.voxels);
}
