#pragma once
#include "common/Types.hpp"
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <entt/entity/fwd.hpp>

namespace voxelmmo {

class Chunk;
class ChunkRegistry;

/**
 * @brief Per-chunk serialization state.
 *
 * Tracks which chunks have been serialized and how many deltas 
 * have been built since the last snapshot.
 */
struct ChunkSerializationState {
    uint32_t lastSnapshotTick{0};   // Tick of last snapshot or snapshot delta
    size_t deltaCount{0};           // Number of tick deltas since last snapshot
    bool hasBeenSerialized{false};  // True after first snapshot
};

/**
 * @brief Serialization buffers and orchestration for chunk messages.
 *
 * GameEngine uses this to serialize all chunk messages into a single
 * concatenated buffer, which is then broadcast to all gateways.
 * All gateways receive the same data (chunks are not filtered per-gateway).
 */
class ChunkSerializer {
public:
    /** Concatenated chunk messages: [chunk1_msg][chunk2_msg]... */
    std::vector<uint8_t> chunkBuf;

    /** SELF_ENTITY messages for newly created players. */
    std::vector<uint8_t> selfEntityBuf;

    /** Reusable scratch buffer for LZ4 compression. */
    std::vector<uint8_t> scratch;

    ChunkSerializer();
    ~ChunkSerializer() = default;

    // Non-copyable, movable
    ChunkSerializer(const ChunkSerializer&) = delete;
    ChunkSerializer& operator=(const ChunkSerializer&) = delete;
    ChunkSerializer(ChunkSerializer&&) = default;
    ChunkSerializer& operator=(ChunkSerializer&&) = default;

    /** Clear output buffers (preserves scratch capacity). */
    void clear();

    /** Check if any chunk data was serialized. */
    bool hasChunkData() const;

    /** Check if any self entity data was serialized. */
    bool hasSelfEntityData() const;

    /**
     * @brief Serialize all active chunks into the concatenated buffer.
     *
     * Each chunk will have a snapshot, snapshot delta, or tick delta built
     * based on its serialization state. All messages are appended to chunkBuf.
     *
     * @param registry The ECS registry.
     * @param chunkRegistry The chunk registry containing all chunks.
     * @param tick The current server tick.
     * @return Number of chunks that produced output.
     */
    size_t serializeAllChunks(entt::registry& registry, ChunkRegistry& chunkRegistry, uint32_t tick);

    /**
     * @brief Get a pointer to the serialized chunk data.
     * @return Pointer to chunkBuf data (valid until next clear()).
     */
    const uint8_t* getChunkData() const;

    /**
     * @brief Get the size of serialized chunk data.
     */
    size_t getChunkDataSize() const;

private:
    /** Track serialization state for each chunk. */
    std::unordered_map<ChunkId, ChunkSerializationState> chunkStates_;

    /** Ensure scratch has at least minSize capacity. */
    void ensureScratch(size_t minSize);
};

} // namespace voxelmmo
