#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include "game/WorldChunk.hpp"
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"

using namespace voxelmmo;

TEST_CASE("DEBUG - cy=1 contents") {
    std::cout << "CHUNK_SIZE_X = " << (int)CHUNK_SIZE_X << std::endl;
    std::cout << "CHUNK_SIZE_Y = " << (int)CHUNK_SIZE_Y << std::endl;
    std::cout << "CHUNK_SIZE_Z = " << (int)CHUNK_SIZE_Z << std::endl;
    
    WorldChunk chunk;
    chunk.generate(0, 1, 0);
    
    int stoneCount = 0, airCount = 0;
    for (size_t i = 0; i < std::min(size_t(100), CHUNK_VOXEL_COUNT); ++i) {
        if (chunk.voxels[i] == VoxelTypes::STONE) stoneCount++;
        else if (chunk.voxels[i] == VoxelTypes::AIR) airCount++;
    }
    std::cout << "First 100 voxels: STONE=" << stoneCount << " AIR=" << airCount << std::endl;
    
    // Print worldY for first few positions
    int cy = 1;
    for (int y = 0; y < 5; y++) {
        int worldY = cy * CHUNK_SIZE_Y + y;
        std::cout << "cy=" << cy << " y=" << y << " -> worldY=" << worldY << std::endl;
    }
    
    REQUIRE(true);
}
