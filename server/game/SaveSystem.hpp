#pragma once
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include "game/WorldGenerator.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace voxelmmo {

// Forward declaration
class ChunkRegistry;

/**
 * @brief Persistent storage system for game world data.
 *
 * Manages save/load of:
 *   - Global game state (seed, generator type, metadata) as JSON
 *   - Chunk voxel data as binary files (one file per chunk)
 *
 * Save directory structure:
 *   saves/<gameKey>/
 *     global.json          - Game configuration and metadata
 *     chunks/
 *       <packed_chunk_id>.chunk  - Individual chunk voxel data
 *
 * Chunk file format:
 *   [4 bytes]  Magic "VMMO"
 *   [4 bytes]  Format version (uint32 = 1)
 *   [8 bytes]  ChunkId packed value
 *   [4 bytes]  Flags (bit 0: compressed)
 *   [4 bytes]  Uncompressed data size (uint32 = 32768)
 *   [4 bytes]  Stored data size (uint32)
 *   [N bytes]  Voxel data (raw or LZ4 compressed)
 */
class SaveSystem {
public:
    /** Default game key (hardcoded as per requirements). */
    static constexpr const char* DEFAULT_GAME_KEY = "voxelmmo_default";

    /** Current save format version. */
    static constexpr uint32_t FORMAT_VERSION = 1;

    /** Magic bytes for chunk files: "VMMO" */
    static constexpr uint32_t CHUNK_MAGIC = 0x4F4D4D56; // 'V' 'M' 'M' 'O'

    /** Chunk file flags. */
    enum ChunkFlags : uint32_t {
        CHUNK_FLAG_COMPRESSED = 0x01,
    };

    /** Global game state stored in global.json. */
    struct GlobalState {
        std::string gameKey;
        uint32_t version = 1;
        uint32_t seed = 0;
        GeneratorType generatorType = GeneratorType::NORMAL;
        std::string createdAt;
        std::string lastSavedAt;
    };

    /**
     * @brief Construct SaveSystem for a specific game key.
     * @param gameKey Unique identifier for this game save.
     */
    explicit SaveSystem(const std::string& gameKey = DEFAULT_GAME_KEY);

    /**
     * @brief Load global state or create new one from CLI parameters.
     *
     * If a saved global state exists, it is loaded and stored internally.
     * If not, a new state is created using the provided CLI parameters,
     * saved to disk, and stored internally.
     *
     * @param cliSeed Seed from command line (used if no saved state).
     * @param cliType Generator type from command line.
     */
    void loadOrCreateGlobalState(uint32_t cliSeed, GeneratorType cliType);

    /**
     * @brief Save global state to disk.
     * Updates lastSavedAt timestamp before saving.
     */
    void saveGlobalState();

    /** @brief Get the current global state. */
    const GlobalState& getGlobalState() const { return globalState_; }

    /** @brief Get the seed from global state. */
    uint32_t getSeed() const { return globalState_.seed; }

    /** @brief Get the generator type from global state. */
    GeneratorType getGeneratorType() const { return globalState_.generatorType; }

    /**
     * @brief Check if a chunk has saved data on disk.
     * @param id The chunk ID to check.
     * @return true if saved chunk file exists.
     */
    bool hasSavedChunk(ChunkId id) const;

    /**
     * @brief Load voxel data for a chunk from disk.
     * @param id The chunk ID to load.
     * @param outVoxels Output buffer, must be sized to CHUNK_VOXEL_COUNT.
     * @return true if loaded successfully.
     */
    bool loadChunkVoxels(ChunkId id, std::vector<VoxelType>& outVoxels);

    /**
     * @brief Save voxel data for a chunk to disk.
     * @param id The chunk ID to save.
     * @param voxels Voxel data buffer (CHUNK_VOXEL_COUNT bytes).
     * @return true if saved successfully.
     */
    bool saveChunkVoxels(ChunkId id, const std::vector<VoxelType>& voxels);

    /**
     * @brief Save all loaded chunks in the registry.
     *
     * Iterates through all chunks in the registry and saves their voxel data.
     * Note: Only saves voxel data, not entity state.
     *
     * @param registry The chunk registry containing all loaded chunks.
     * @return Number of chunks saved.
     */
    size_t saveAllChunks(const ChunkRegistry& registry);

    /**
     * @brief Save only active chunks in the registry.
     *
     * Used during shutdown to save only chunks that have entities.
     * Unwatched chunks are already saved when unloaded.
     *
     * @param registry The chunk registry containing chunks.
     * @return Number of chunks saved.
     */
    size_t saveActiveChunks(const ChunkRegistry& registry);

    /** @brief Get the base save directory for this game key. */
    const std::string& getBaseDir() const { return baseDir_; }

    /** @brief Get the chunks subdirectory path. */
    const std::string& getChunksDir() const { return chunksDir_; }

    /** @brief Get current ISO8601 timestamp string. */
    static std::string getCurrentTimestamp();

private:
    std::string gameKey_;
    std::string baseDir_;
    std::string chunksDir_;
    GlobalState globalState_;

    /**
     * @brief Generate chunk filename from ChunkId.
     *
     * Uses the packed ChunkId value in hex format for a unique,
     * filesystem-safe filename.
     *
     * @param id The chunk ID.
     * @return Full path to the chunk file.
     */
    std::string getChunkFilename(ChunkId id) const;

    /**
     * @brief Ensure save directories exist.
     * @return true if directories exist or were created successfully.
     */
    bool ensureDirectoriesExist();
};

} // namespace voxelmmo
