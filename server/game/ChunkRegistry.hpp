#pragma once
#include "Chunk.hpp"
#include "WorldGenerator.hpp"
#include "common/Types.hpp"
#include "game/components/PlayerComponent.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <memory>

namespace voxelmmo {

/**
 * @brief Central registry for all chunks in the game world.
 *
 * Owns the chunk map and controls chunk lifecycle:
 * - Generation: creates chunk voxel data
 * - Activation: spawns entities in the chunk
 * - Deactivation: removes entities from the chunk
 *
 * Provides read-only access via getChunk() for systems that only need
 * to query chunk state (physics, serialization, etc.).
 *
 * Note: For now, chunks are always activated when generated (simplification).
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
     * For now, chunks are always activated when generated (simplification).
     * This creates the chunk, generates voxels, and marks it as activated.
     *
     * @param generator WorldGenerator for terrain generation.
     * @param id Chunk ID to generate.
     * @param registry ECS registry (for entity activation).
     * @param tick Current server tick.
     * @return Pointer to the generated chunk.
     */
    Chunk* generate(WorldGenerator& generator, ChunkId id,
                    entt::registry& registry, uint32_t tick) {
        auto it = chunks_.find(id);
        if (it != chunks_.end()) {
            return it->second.get();  // Already exists
        }

        auto chunk = std::make_unique<Chunk>(id);
        generator.generate(chunk->world.voxels, id.x(), id.y(), id.z());
        
        // For now: activate immediately on generation
        chunk->activated = true;
        generator.generateEntities(id, registry, tick);
        
        Chunk* ptr = chunk.get();
        chunks_[id] = std::move(chunk);
        return ptr;
    }

    /**
     * @brief Activate a chunk, spawning its entities.
     *
     * If the chunk doesn't exist, it will be generated first.
     * If already activated, this is a no-op.
     *
     * @param generator WorldGenerator for terrain/entity generation.
     * @param id Chunk ID to activate.
     * @param registry ECS registry (for entity activation).
     * @param tick Current server tick.
     * @return Pointer to the activated chunk.
     */
    Chunk* activate(WorldGenerator& generator, ChunkId id,
                    entt::registry& registry, uint32_t tick) {
        auto it = chunks_.find(id);
        if (it == chunks_.end()) {
            // Chunk doesn't exist - generate it (which also activates)
            return generate(generator, id, registry, tick);
        }

        Chunk* chunk = it->second.get();
        if (chunk->activated) {
            return chunk;  // Already activated
        }

        // Activate: spawn entities
        chunk->activated = true;
        generator.generateEntities(id, registry, tick);
        return chunk;
    }

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
    bool deactivate(ChunkId id, entt::registry& registry) {
        auto it = chunks_.find(id);
        if (it == chunks_.end() || !it->second->activated) {
            return false;
        }

        Chunk* chunk = it->second.get();
        chunk->activated = false;

        // Remove all non-player entities from this chunk
        std::vector<entt::entity> toRemove;
        toRemove.reserve(chunk->entities.size());
        
        for (entt::entity ent : chunk->entities) {
            // Keep players (they persist across chunk deactivation)
            if (registry.valid(ent) && !registry.all_of<PlayerComponent>(ent)) {
                toRemove.push_back(ent);
            }
        }

        for (entt::entity ent : toRemove) {
            chunk->entities.erase(ent);
            if (registry.valid(ent)) {
                registry.destroy(ent);
            }
        }

        return true;
    }

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
