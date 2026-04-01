#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "TestUtils.hpp"
#include "game/SaveSystem.hpp"
#include <filesystem>

namespace fs = std::filesystem;
using namespace voxelmmo;

TEST_CASE("Chunks are saved after player joins", "[save][integration]") {
    // Use a unique test game key
    const std::string testGameKey = "test_integration_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string saveDir = "saves/" + testGameKey;
    
    // Clean up any existing test directory
    fs::remove_all(saveDir);
    
    // Create a GameEngine with custom game key for test isolation
    GameEngine engine(12345, GeneratorType::TEST, true, std::nullopt, testGameKey);
    
    // Register a gateway
    engine.registerGateway(0);
    
    // Simulate player connecting and sending JOIN
    // Use session token that derives to PlayerId = 1
    std::array<uint8_t, 16> sessionToken{};
    sessionToken[0] = 1;  // PlayerId = 1 in little-endian
    PlayerId pid;
    std::memcpy(&pid, sessionToken.data(), sizeof(PlayerId));
    engine.registerPlayer(0, pid);
    
    // Build JOIN message: [type(1)] [size(2)] [entityType(1)] [sessionToken(16)]
    uint8_t joinMsg[21] = {
        static_cast<uint8_t>(ClientMessageType::JOIN),
        21, 0,  // size = 21 bytes
        static_cast<uint8_t>(EntityType::PLAYER)
    };
    std::memcpy(joinMsg + 4, sessionToken.data(), 16);
    engine.handlePlayerInput(pid, joinMsg, sizeof(joinMsg));
    
    // Run a few ticks to let chunks generate
    for (int i = 0; i < 10; ++i) {
        engine.tick();
    }
    
    // Check that chunks were generated
    size_t chunkCountBefore = engine.getChunkRegistry().getAllChunks().size();
    REQUIRE(chunkCountBefore > 0);  // Should have spawn chunks
    
    // Save all chunks
    size_t savedCount = engine.getSaveSystem()->saveAllChunks(engine.getChunkRegistry());
    REQUIRE(savedCount == chunkCountBefore);
    
    // Verify chunk files exist
    size_t filesInDir = 0;
    std::string chunksDir = saveDir + "/chunks";
    for (const auto& entry : fs::directory_iterator(chunksDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".chunk") {
            ++filesInDir;
        }
    }
    REQUIRE(filesInDir == chunkCountBefore);
    
    // Now verify chunks can be loaded from save
    ChunkId testChunkId = ChunkId::make(0, 0, 0);  // Spawn chunk
    REQUIRE(engine.getSaveSystem()->hasSavedChunk(testChunkId));
    
    std::vector<VoxelType> loadedVoxels;
    REQUIRE(engine.getSaveSystem()->loadChunkVoxels(testChunkId, loadedVoxels));
    REQUIRE(loadedVoxels.size() == CHUNK_VOXEL_COUNT);
    
    // Clean up
    fs::remove_all(saveDir);
}

TEST_CASE("Saved chunks persist across server restarts", "[save][integration]") {
    const std::string testGameKey = "test_persist_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string saveDir = "saves/" + testGameKey;
    
    // Clean up
    fs::remove_all(saveDir);
    
    ChunkId chunkId = ChunkId::make(0, 0, 0);
    std::vector<VoxelType> originalVoxels;
    
    // First "server session" - generate and modify chunks
    {
        GameEngine engine(12345, GeneratorType::TEST, true, std::nullopt, testGameKey);
        engine.registerGateway(0);
        
        PlayerId pid = 1;
        engine.registerPlayer(0, pid);
        
        uint8_t joinMsg[21] = {
            static_cast<uint8_t>(ClientMessageType::JOIN), 21, 0,
            static_cast<uint8_t>(EntityType::PLAYER),
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        engine.handlePlayerInput(pid, joinMsg, sizeof(joinMsg));
        
        for (int i = 0; i < 5; ++i) engine.tick();
        
        // Modify a voxel
        auto* chunk = engine.getChunkRegistry().getChunkMutable(chunkId);
        REQUIRE(chunk != nullptr);
        chunk->world.setVoxel(5, 5, 5, VoxelTypes::STONE);
        originalVoxels = chunk->world.voxels;  // Copy voxels
        
        // Save
        size_t saved = engine.getSaveSystem()->saveAllChunks(engine.getChunkRegistry());
        REQUIRE(saved > 0);
    }
    
    // Second "server session" - verify chunks load with modifications
    {
        GameEngine engine(12345, GeneratorType::TEST, true, std::nullopt, testGameKey);
        // Load the chunk
        std::vector<VoxelType> loadedVoxels;
        REQUIRE(engine.getSaveSystem()->loadChunkVoxels(chunkId, loadedVoxels));
        REQUIRE(loadedVoxels.size() == CHUNK_VOXEL_COUNT);
        
        // Verify modification persisted
        VoxelIndex idx = voxelIndexFromPos(5, 5, 5);
        CHECK(loadedVoxels[idx] == VoxelTypes::STONE);
        CHECK(loadedVoxels[idx] == originalVoxels[idx]);
    }
    
    // Clean up
    fs::remove_all(saveDir);
}

TEST_CASE("Chunk loading prevents regeneration", "[save][integration]") {
    const std::string testGameKey = "test_load_prevents_regen_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string saveDir = "saves/" + testGameKey;
    
    // Clean up
    fs::remove_all(saveDir);
    
    ChunkId chunkId = ChunkId::make(0, 0, 0);
    
    // First session - generate and save chunks
    {
        GameEngine engine(12345, GeneratorType::TEST, true, std::nullopt, testGameKey);
        engine.registerGateway(0);
        
        PlayerId pid = 1;
        engine.registerPlayer(0, pid);
        
        uint8_t joinMsg[21] = {
            static_cast<uint8_t>(ClientMessageType::JOIN), 21, 0,
            static_cast<uint8_t>(EntityType::PLAYER),
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        engine.handlePlayerInput(pid, joinMsg, sizeof(joinMsg));
        
        for (int i = 0; i < 5; ++i) engine.tick();
        
        // Modify a voxel to prove it's saved
        auto* chunk = engine.getChunkRegistry().getChunkMutable(chunkId);
        REQUIRE(chunk != nullptr);
        chunk->world.setVoxel(10, 10, 10, VoxelTypes::DIRT);
        
        engine.getSaveSystem()->saveAllChunks(engine.getChunkRegistry());
    }
    
    // Second session - create new engine with different seed but same save
    // Chunks should load from save, not regenerate with new seed
    {
        // Note: We use a different seed (99999) and different type (NORMAL)
        // But chunks should still load from save with the original data
        GameEngine engine(99999, GeneratorType::NORMAL, true, std::nullopt, testGameKey);
        engine.registerGateway(0);
        
        PlayerId pid = 1;
        engine.registerPlayer(0, pid);
        
        uint8_t joinMsg[21] = {
            static_cast<uint8_t>(ClientMessageType::JOIN), 21, 0,
            static_cast<uint8_t>(EntityType::PLAYER),
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        engine.handlePlayerInput(pid, joinMsg, sizeof(joinMsg));
        
        for (int i = 0; i < 5; ++i) engine.tick();
        
        // The chunk should have been loaded from save, not regenerated
        auto* chunk = engine.getChunkRegistry().getChunkMutable(chunkId);
        REQUIRE(chunk != nullptr);
        
        // Check that our modification is still there
        VoxelIndex idx = voxelIndexFromPos(10, 10, 10);
        CHECK(chunk->world.voxels[idx] == VoxelTypes::DIRT);
    }
    
    // Clean up
    fs::remove_all(saveDir);
}
