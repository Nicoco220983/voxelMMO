#pragma once
#include "WorldChunk.hpp"
#include "game/entities/BaseEntity.hpp"
#include "common/Types.hpp"
#include "common/MessageTypes.hpp"
#include "common/ChunkState.hpp"
#include <entt/entt.hpp>
#include <set>
#include <memory>
#include <unordered_map>
#include <cstdint>

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

    std::set<EntityId> entities;

    /** @brief Serialised state cache (snapshot, deltas, scratch). */
    ChunkState state;

    explicit Chunk(ChunkId chunkId);

    /**
     * @brief Build a full snapshot and store it in state.snapshot.
     *
     * Voxels are LZ4-compressed directly from world.voxels (zero copy for
     * the voxel section).  Entities are serialised raw into state.scratch,
     * then copied or LZ4-compressed into state.snapshot depending on size.
     *
     * @return const-ref to state.snapshot (always non-empty after this call).
     */
    const std::vector<uint8_t>& buildSnapshot(
        entt::registry& reg,
        const std::unordered_map<EntityId, std::unique_ptr<BaseEntity>>& entityMap);

    /**
     * @brief Build a snapshot delta and store it in state.snapshotDelta.
     *
     * Payload is serialised raw directly into state.snapshotDelta, then
     * compressed in-place via state.scratch if it exceeds the threshold.
     *
     * @return const-ref to state.snapshotDelta.
     *         Empty if both voxel and entity deltas are empty.
     */
    const std::vector<uint8_t>& buildSnapshotDelta(
        entt::registry& reg,
        const std::unordered_map<EntityId, std::unique_ptr<BaseEntity>>& entityMap);

    /**
     * @brief Build a tick delta and append it to state.tickDeltas.
     *
     * Same compression strategy as buildSnapshotDelta.
     *
     * @return const-ref to state.tickDeltas.back().
     *         state.tickDeltas is unchanged (no new entry) if both deltas are empty.
     */
    const std::vector<uint8_t>& buildTickDelta(
        entt::registry& reg,
        const std::unordered_map<EntityId, std::unique_ptr<BaseEntity>>& entityMap);

    /** @brief Byte length of the always-uncompressed message header. */
    static constexpr size_t HEADER_SIZE = 1 + sizeof(int64_t); // type(1) + ChunkId(8)

private:
    /** @brief Write type byte + ChunkId into buf; return bytes written (= HEADER_SIZE). */
    size_t writeHeader(uint8_t* buf, ChunkMessageType msgType) const;

    /**
     * @brief If buf's payload exceeds LZ4_COMPRESSION_THRESHOLD, copy the
     *        payload to state.scratch, LZ4-compress it back into buf
     *        (replacing the payload), prepend the uncompressed-size int32,
     *        and update the type byte to the *_COMPRESSED variant.
     */
    void maybeCompressDelta(std::vector<uint8_t>& buf, ChunkMessageType compressedType);
};

} // namespace voxelmmo
