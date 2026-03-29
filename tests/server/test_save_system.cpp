#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "game/SaveSystem.hpp"
#include "game/ChunkRegistry.hpp"
#include "game/WorldGenerator.hpp"
#include "game/Chunk.hpp"
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;
using namespace voxelmmo;

TEST_CASE("SaveSystem creates correct directory structure", "[save]") {
    const std::string testGameKey = "test_save_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    // Clean up any existing test directory
    fs::remove_all("saves/" + testGameKey);
    
    {
        SaveSystem saveSystem(testGameKey);
        
        CHECK(fs::exists(saveSystem.getBaseDir()));
        CHECK(fs::exists(saveSystem.getChunksDir()));
        CHECK(saveSystem.getBaseDir() == "saves/" + testGameKey);
        CHECK(saveSystem.getChunksDir() == "saves/" + testGameKey + "/chunks");
    }
    
    // Clean up
    fs::remove_all("saves/" + testGameKey);
}

TEST_CASE("SaveSystem saves and loads global state", "[save]") {
    const std::string testGameKey = "test_global_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    // Clean up any existing test directory
    fs::remove_all("saves/" + testGameKey);
    
    uint32_t testSeed = 12345;
    GeneratorType testType = GeneratorType::NORMAL;
    
    // Create new save
    {
        SaveSystem saveSystem(testGameKey);
        saveSystem.loadOrCreateGlobalState(testSeed, testType);
        
        const auto& state = saveSystem.getGlobalState();
        CHECK(state.gameKey == testGameKey);
        CHECK(state.seed == testSeed);
        CHECK(state.generatorType == testType);
        CHECK(state.version == 1);
        CHECK(!state.createdAt.empty());
        CHECK(!state.lastSavedAt.empty());
    }
    
    // Load existing save with different CLI params (should use saved values)
    {
        SaveSystem saveSystem(testGameKey);
        saveSystem.loadOrCreateGlobalState(99999, GeneratorType::TEST);
        
        const auto& loadedState = saveSystem.getGlobalState();
        // Should load saved values, not CLI values
        CHECK(loadedState.seed == testSeed);
        CHECK(loadedState.generatorType == testType);
        CHECK(loadedState.gameKey == testGameKey);
    }
    
    // Clean up
    fs::remove_all("saves/" + testGameKey);
}

TEST_CASE("SaveSystem saves and loads chunk voxels", "[save]") {
    const std::string testGameKey = "test_chunk_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    // Clean up any existing test directory
    fs::remove_all("saves/" + testGameKey);
    
    ChunkId testChunkId = ChunkId::make(0, 0, 0);  // Chunk at (0,0,0)
    
    // Create and save chunk data
    {
        SaveSystem saveSystem(testGameKey);
        
        // Create test voxel data with a pattern
        std::vector<VoxelType> voxels(CHUNK_VOXEL_COUNT);
        for (size_t i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
            voxels[i] = static_cast<VoxelType>(i % 256);
        }
        
        CHECK(saveSystem.saveChunkVoxels(testChunkId, voxels));
        CHECK(saveSystem.hasSavedChunk(testChunkId));
    }
    
    // Load chunk data
    {
        SaveSystem saveSystem(testGameKey);
        
        CHECK(saveSystem.hasSavedChunk(testChunkId));
        
        std::vector<VoxelType> loadedVoxels;
        CHECK(saveSystem.loadChunkVoxels(testChunkId, loadedVoxels));
        CHECK(loadedVoxels.size() == CHUNK_VOXEL_COUNT);
        
        // Verify data integrity
        for (size_t i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
            CHECK(loadedVoxels[i] == static_cast<VoxelType>(i % 256));
        }
    }
    
    // Clean up
    fs::remove_all("saves/" + testGameKey);
}

TEST_CASE("SaveSystem hasSavedChunk returns false for non-existent chunks", "[save]") {
    const std::string testGameKey = "test_missing_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    // Clean up any existing test directory
    fs::remove_all("saves/" + testGameKey);
    
    SaveSystem saveSystem(testGameKey);
    
    ChunkId nonExistentChunk = ChunkId::make(999, 999, 999);
    CHECK(!saveSystem.hasSavedChunk(nonExistentChunk));
    
    std::vector<VoxelType> voxels;
    CHECK(!saveSystem.loadChunkVoxels(nonExistentChunk, voxels));
    
    // Clean up
    fs::remove_all("saves/" + testGameKey);
}

TEST_CASE("SaveSystem chunk filename uses packed ChunkId", "[save]") {
    const std::string testGameKey = "test_filename_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    fs::remove_all("saves/" + testGameKey);
    
    SaveSystem saveSystem(testGameKey);
    
    // Create a chunk with known coordinates
    ChunkId chunkId = ChunkId::make(1, 2, 3);  // y=1, x=2, z=3
    
    // Verify hasSavedChunk uses the correct filename format
    // The filename should be based on the packed value
    CHECK(!saveSystem.hasSavedChunk(chunkId));
    
    // Save some data
    std::vector<VoxelType> voxels(CHUNK_VOXEL_COUNT, VoxelTypes::STONE);
    CHECK(saveSystem.saveChunkVoxels(chunkId, voxels));
    
    // Verify it can be found
    CHECK(saveSystem.hasSavedChunk(chunkId));
    
    // Clean up
    fs::remove_all("saves/" + testGameKey);
}
