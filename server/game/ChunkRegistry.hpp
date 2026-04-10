#pragma once
#include "Chunk.hpp"
#include "common/Types.hpp"
#include "game/components/PlayerComponent.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>
#include <functional>

namespace voxelmmo {

// Forward declarations
class WorldGenerator;
class EntityFactory;
class SaveSystem;

/**
 * @brief Central registry for all chunks in the game world.
 *
 * Owns the chunk map and controls chunk lifecycle:
 * - Generation: creates chunk voxel data
 * - Activation: marks chunks as active and generates entities
 * - Deactivation: removes entities from the chunk
 *
 * Provides read-only access via getChunk() for systems that only need
 * to query chunk state (physics, serialization, etc.).
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
     * If a saveSystem is provided and a saved chunk exists, loads from save instead.
     * The chunk is NOT activated by this method - activation is a separate step.
     *
     * @param generator WorldGenerator for terrain generation.
     * @param id Chunk ID to generate.
     * @param saveSystem Optional SaveSystem to load saved chunks from.
     * @return Pointer to the generated/loaded chunk.
     */
    Chunk* generate(WorldGenerator& generator, ChunkId id, SaveSystem* saveSystem = nullptr);

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
     * Entity generation is performed automatically using the provided WorldGenerator.
     *
     * @param id Chunk ID to activate.
     * @param generator WorldGenerator for entity generation.
     * @param entityFactory Factory to queue entity spawn requests.
     * @param tick Current server tick.
     * @return Pointer to the activated chunk, or nullptr if chunk doesn't exist.
     */
    Chunk* activate(ChunkId id, WorldGenerator& generator, EntityFactory& entityFactory, uint32_t tick);

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
     * @brief Unload a chunk from the registry.
     *
     * This removes the chunk entirely from memory. If the chunk is still
     * activated, it will be deactivated first (marking all non-player
     * entities for deletion). The chunk voxel data should be saved before
     * calling this.
     *
     * @param id Chunk ID to unload.
     * @param registry ECS registry (needed for deactivation if still active).
     * @return true if the chunk was unloaded, false if not found.
     */
    bool unload(ChunkId id, entt::registry& registry);

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

    /**
     * @brief Check if a chunk is active (entities spawned).
     * @param id The chunk ID to check.
     * @return true if the chunk exists and is activated.
     */
    bool isActive(ChunkId id) const;

    /**
     * @brief Add a non-player entity to its chunk.
     *
     * The entity is added to the chunk's entities set based on its position.
     *
     * @param chunkId The chunk ID where the entity should be added.
     * @param entity The entity handle to add.
     * @return true if the entity was added, false if chunk not found.
     */
    bool addEntity(ChunkId chunkId, entt::entity entity);

    /**
     * @brief Add a player entity to its chunk.
     *
     * The entity is added to both the chunk's entities set and presentPlayers set.
     *
     * @param chunkId The chunk ID where the player should be added.
     * @param entity The entity handle to add.
     * @param playerId The player ID to add to presentPlayers.
     * @return true if the entity was added, false if chunk not found.
     */
    bool addPlayerEntity(ChunkId chunkId, entt::entity entity, PlayerId playerId);

    /**
     * @brief Remove an entity from its chunk's entity set.
     *
     * The entity is removed from the chunk's entities set. For players,
     * also removes from presentPlayers set.
     *
     * @param chunkId The chunk ID where the entity should be removed from.
     * @param entity The entity handle to remove.
     * @param playerId Optional player ID to remove from presentPlayers (0 = not a player).
     * @return true if the entity was removed, false if chunk not found.
     */
    bool removeEntity(ChunkId chunkId, entt::entity entity, PlayerId playerId = 0);

private:
    std::unordered_map<ChunkId, std::unique_ptr<Chunk>> chunks_;
};

} // namespace voxelmmo
