#pragma once
#include "WorldChunk.hpp"
#include "common/Types.hpp"
#include "common/MessageTypes.hpp"
#include "common/ChunkState.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include <entt/entt.hpp>
#include <set>
#include <memory>
#include <unordered_map>
#include <cstdint>

namespace voxelmmo {
    // Chunk header: [entity_type(1)][size(2)][chunk_id(8)][tick(4)] = 15 bytes
    static constexpr size_t CHUNK_MESSAGE_HEADER_SIZE = 1 + 2 + 8 + 4;
}

namespace voxelmmo {
    class WorldGenerator;
}

namespace voxelmmo {

/**
 * @brief Owns all simulation state for one chunk tile.
 *
 * Serialisation is driven by the three build*() methods which write into
 * ChunkState::state.  Each method returns a const-ref to the relevant buffer
 * inside state; an empty vector means there was nothing to send.
 *
 * Buffer ownership / thread-safety:
 *   Each Chunk owns its ChunkState (snapshot, deltas, scratch).  Because the
 *   game loop will eventually serialise chunks in parallel, no buffer is
 *   shared across Chunk instances – each thread works on its own Chunk.
 */
class Chunk {
public:
    ChunkId id;

    /** @brief Players whose avatar is inside this chunk. */
    std::set<PlayerId> presentPlayers;

    /** @brief Players watching this chunk (present + nearby). */
    std::set<PlayerId> watchingPlayers;

    WorldChunk world;

    /**
     * @brief Set of entities currently resident in this chunk.
     *
     * Entities are tracked by their entt handle. The GlobalEntityId (stable across
     * chunk moves) is stored in GlobalEntityIdComponent and used on the wire.
     */
    std::set<entt::entity> entities;
    
    /**
     * @brief Set of entities that have left this chunk this tick.
     *
     * These entities have been removed from `entities` and are waiting
     * to be added to their new chunk. After processing, this set is cleared.
     */
    std::set<entt::entity> leftEntities;

    /** @brief Serialised state cache (snapshot, deltas, scratch). */
    ChunkState state;

    /** @brief True if the chunk has been activated (entities spawned). */
    bool activated = false;

    /** @brief Counter for delta type selection (reset after snapshot). */
    uint32_t deltaCallCount = 0;

    explicit Chunk(ChunkId chunkId);

    /**
     * @brief Build a full snapshot and store it in state.snapshot.
     *
     * Voxels are LZ4-compressed directly from world.voxels (zero copy for
     * the voxel section).  Entities are serialised raw into state.scratch,
     * then copied or LZ4-compressed into state.snapshot depending on size.
     * Also clears state.deltas (the snapshot supersedes all previous deltas).
     *
     * @return const-ref to state.snapshot (always non-empty after this call).
     */
    const std::vector<uint8_t>& buildSnapshot(entt::registry& reg, uint32_t tickCount);

    /**
     * @brief Build a snapshot delta and append it to state.deltas.
     *
     * Payload is built into a local staging buffer, optionally LZ4-compressed
     * via state.scratch, then appended to the unified state.deltas buffer.
     * Sets state.hasNewDelta = true iff anything was appended.
     *
     * @return true if a delta was appended; false if nothing changed.
     */
    bool buildSnapshotDelta(entt::registry& reg, uint32_t tickCount);

    /**
     * @brief Build a tick delta and append it to state.deltas.
     *
     * Same strategy as buildSnapshotDelta.
     *
     * @return true if a delta was appended; false if nothing changed.
     */
    bool buildTickDelta(entt::registry& reg, uint32_t tickCount);

    /**
     * @brief Update chunk state based on current conditions.
     *
     * Logic:
     * - If no snapshot exists (state.snapshot empty),
     *   calls buildSnapshot.
     * - Else if first call after snapshot OR every 20 calls, calls buildSnapshotDelta.
     * - Otherwise, calls buildTickDelta.
     *
     * @return true if any state was built/appended; false otherwise.
     */
    bool updateState(entt::registry& reg, uint32_t tickCount);

    /** @brief Byte length of the chunk message header (including entity_type + size prefix). */
    static constexpr size_t HEADER_SIZE = CHUNK_MESSAGE_HEADER_SIZE; // entity_type(1) + size(2) + ChunkId(8) + tick(4)

private:
    /**
     * @brief Write chunk message header into buf; return bytes written (= HEADER_SIZE).
     * Format: [type(1)][size(2)][chunk_id(8)][tick(4)]
     * The size field is written as 0 and must be filled in later when the final size is known.
     */
    size_t writeHeader(uint8_t* buf, ServerMessageType msgType, uint32_t tickCount) const;

    /**
     * @brief If buf's payload exceeds LZ4_COMPRESSION_THRESHOLD, copy the
     *        payload to state.scratch, LZ4-compress it back into buf
     *        (replacing the payload), prepend the uncompressed-size int32,
     *        and update the type byte to the *_COMPRESSED variant.
     */
    void maybeCompressDelta(std::vector<uint8_t>& buf, ServerMessageType compressedType);

    /**
     * @brief Shared implementation for buildSnapshotDelta and buildTickDelta.
     *
     * @param voxelDeltas    The per-granularity changed-voxel list.
     * @param flagsField     Pointer-to-member selecting snapshotDirtyFlags or tickDirtyFlags.
     * @param rawType        Message type written into the header (uncompressed variant).
     * @param compressedType Message type to use if LZ4 compression is applied.
     * @param clearSnapshot  If true, clear snapshotDirtyFlags after serializing.
     * @param clearTick      If true, clear tickDirtyFlags after serializing.
     */
    bool buildDeltaImpl(
        entt::registry& reg,
        uint32_t tickCount,
        const std::vector<std::pair<VoxelIndex, VoxelType>>& voxelDeltas,
        uint8_t DirtyComponent::* flagsField,
        ServerMessageType rawType,
        ServerMessageType compressedType,
        bool clearSnapshot,
        bool clearTick);
};

} // namespace voxelmmo
