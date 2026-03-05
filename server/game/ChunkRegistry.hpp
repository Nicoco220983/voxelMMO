#pragma once
#include "Chunk.hpp"
#include "common/Types.hpp"
#include "game/components/PlayerComponent.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>

namespace voxelmmo {

// Forward declaration
class WorldGenerator;

/**
 * @brief Central registry for all chunks in the game world.
 *
 * Owns the chunk map and controls chunk lifecycle:
 * - Generation: creates chunk voxel data
 * - Activation: marks chunks as active (entity generation is caller's responsibility)
 * - Deactivation: removes entities from the chunk
 *
 * Provides read-only access via getChunk() for systems that only need
 * to query chunk state (physics, serialization, etc.).
 *
 * NOTE: Entity generation is NOT done by ChunkRegistry. Callers should use
 * WorldGenerator::generateEntities() after activating/creating chunks if needed.
 */
class ChunkRegistry {
public:
    ChunkRegistry() = default;
    ~ChunkRegistry() = default;

    // Non-copyable, non-movable (contains registry references)
    ChunkRegistry(const ChunkRegistry&) = delete;
    ChunkRegistry& operator=(const ChunkRegistry&) = delete;
    ChunkRegistry(ChunkRegistry&&) = delete;
    ChunkRegistry& operator=(ChunkRegistry&&) = delete;

    /**
     * @brief Get a chunk by ID (read-only).
     * @return Pointer to the chunk, or nullptr if not loaded.
     */
    const Chunk* getChunk(ChunkId id) const {
        auto it = chunks_.find(id);
        return it != chunks_.end() ? it->second.get() : nullptr;
    }

    /**
     * @brief Get a chunk by ID (non-const access for internal use).
     * @return Pointer to the chunk, or nullptr if not loaded.
     */
    Chunk* getChunkMutable(ChunkId id) {
        auto it = chunks_.find(id);
        return it != chunks_.end() ? it->second.get() : nullptr;
    }

    /**
     * @brief Check if a chunk exists (has been generated).
     */
    bool hasChunk(ChunkId id) const {
        return chunks_.find(id) != chunks_.end();
    }

    /**
     * @brief Generate a chunk's voxel data.
     *
     * Creates the chunk and generates voxels using the provided WorldGenerator.
     * The chunk is NOT activated by this method - activation is a separate step.
     *
     * @param generator WorldGenerator for terrain generation.
     * @param id Chunk ID to generate.
     * @return Pointer to the generated chunk.
     */
    Chunk* generate(WorldGenerator& generator, ChunkId id);

    /**
     * @brief Create or get a chunk without generating voxels.
     *
     * If the chunk doesn't exist, creates it with empty voxels.
     * If it exists, returns the existing chunk.
     *
     * @param id Chunk ID to create/get.
     * @return Pointer to the chunk.
     */
    Chunk* createOrGet(ChunkId id);

    /**
     * @brief Activate a chunk.
     *
     * If the chunk doesn't exist, this returns nullptr (does NOT generate).
     * If already activated, this is a no-op.
     *
     * NOTE: Entity generation is the caller's responsibility. After activating
     * a chunk, call WorldGenerator::generateEntities() if needed.
     *
     * @param id Chunk ID to activate.
     * @return Pointer to the activated chunk, or nullptr if chunk doesn't exist.
     */
    Chunk* activate(ChunkId id);

    /**
     * @brief Generate and activate a chunk in one step.
     *
     * Convenience method that generates voxels and marks as activated.
     * Entity generation is still the caller's responsibility.
     *
     * @param generator WorldGenerator for terrain generation.
     * @param id Chunk ID to generate and activate.
     * @return Pointer to the generated and activated chunk.
     */
    Chunk* generateAndActivate(WorldGenerator& generator, ChunkId id);

    /**
     * @brief Deactivate a chunk, removing all its entities.
     *
     * This removes all non-player entities from the chunk.
     * The chunk voxel data is retained.
     *
     * @param id Chunk ID to deactivate.
     * @param registry ECS registry (for entity destruction).
     * @return true if the chunk was deactivated, false if not found or not active.
     */
    bool deactivate(ChunkId id, entt::registry& registry);

    /**
     * @brief Get all loaded chunks (for iteration).
     * Used by systems that need to process all chunks (serialization, physics).
     */
    const std::unordered_map<ChunkId, std::unique_ptr<Chunk>>& getAllChunks() const {
        return chunks_;
    }

    /**
     * @brief Get all loaded chunks (non-const, for internal use).
     */
    std::unordered_map<ChunkId, std::unique_ptr<Chunk>>& getAllChunksMutable() {
        return chunks_;
    }

    /**
     * @brief Clear all chunks and reset state.
     */
    void clear() {
        chunks_.clear();
    }

private:
    std::unordered_map<ChunkId, std::unique_ptr<Chunk>> chunks_;
};

} // namespace voxelmmo
