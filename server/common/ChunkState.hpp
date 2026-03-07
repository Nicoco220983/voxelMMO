#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <utility>

namespace voxelmmo {

/**
 * @brief Serialised state cache for one chunk.
 *
 * Used symmetrically on both sides of the pipeline:
 *
 *   Game-engine side (Chunk::state):
 *     - snapshot / snapshotTick written by Chunk::buildSnapshot().
 *     - deltas / deltaOffsets written by build*Delta().
 *     - hasNewDelta: set by build*Delta(), cleared by the dispatch loop.
 *     - scratch: reusable staging buffer for compression.
 *
 *   Gateway side (StateManager):
 *     - snapshot / snapshotTick populated by receiveSnapshot().
 *     - deltas / deltaOffsets populated by receiveDelta().
 *     - scratch is unused.
 *
 * Catch-up logic given a recipient's lastStateTick T:
 *   if T < snapshotTick (or T == 0): send snapshot + all deltas
 *   else:                            send deltasNewerThan(T) — may be empty
 *
 * Wire formats stored in each buffer:
 *
 *   snapshot  (always CHUNK_SNAPSHOT_COMPRESSED):
 *     [0]       uint8   ServerMessageType = CHUNK_SNAPSHOT_COMPRESSED
 *     [1:3]     uint16  message size (LE)
 *     [3:11]    int64   ChunkId (LE)
 *     [11:15]   uint32  tick (LE)  ← stored in snapshotTick
 *     [15]      uint8   flags  — bit 0: entity section is LZ4 compressed
 *     …
 *
 *   deltas — concatenated messages, each:
 *     [0]       uint8   ServerMessageType  (CHUNK_SNAPSHOT_DELTA, CHUNK_TICK_DELTA, or *_COMPRESSED)
 *     [1:3]     uint16  message size (LE)
 *     [3:11]    int64   ChunkId (LE)
 *     [11:15]   uint32  tick (LE)  ← indexed in deltaOffsets
 *     …
 */
struct ChunkState {
    // ── Snapshot ─────────────────────────────────────────────────────────────

    /** Latest full snapshot message (ready to send). */
    std::vector<uint8_t> snapshot;

    /** Server tick when snapshot was built. 0 = no snapshot yet. */
    uint32_t snapshotTick{0};

    // ── Unified delta buffer ──────────────────────────────────────────────────

    /**
     * All delta messages accumulated since the last snapshot, concatenated.
     * Cleared when a new snapshot is built.
     */
    std::vector<uint8_t> deltas;

    /**
     * Index into deltas: one entry per appended message.
     * entry.tick   = server tick embedded in that message's header.
     * entry.offset = byte offset where that message starts in deltas.
     *
     * Invariant: deltaOffsets[i].offset < deltaOffsets[i+1].offset.
     * The message at index i spans [offset_i, offset_{i+1}) (or to deltas.end()).
     */
    struct DeltaEntry { uint32_t tick; size_t offset; };
    std::vector<DeltaEntry> deltaOffsets;

    /**
     * Set to true by build*Delta() when a message was appended this tick.
     * Cleared by the serialize dispatch loop after batching.
     */
    bool hasNewDelta{false};

    /** Reusable scratch buffer (game-engine side only). */
    std::vector<uint8_t> scratch;

    // ── Query helpers ─────────────────────────────────────────────────────────

    /**
     * @brief Byte range [begin, end) of messages with tick > lastStateTick.
     *
     * Returns {deltas.size(), deltas.size()} when nothing is newer (caller
     * should check begin != end before sending).
     */
    std::pair<size_t, size_t> deltasNewerThan(uint32_t lastStateTick) const {
        for (const auto& e : deltaOffsets) {
            if (e.tick > lastStateTick) return {e.offset, deltas.size()};
        }
        return {deltas.size(), deltas.size()};
    }

    // ── Receive helpers (gateway side) ────────────────────────────────────────

    /** @brief Store a received full snapshot; discard all cached deltas. */
    void receiveSnapshot(const uint8_t* data, size_t size) {
        snapshot.assign(data, data + size);
        snapshotTick = 0;
        // tick is now at bytes 11-14 in the 15-byte header
        if (size >= 15) std::memcpy(&snapshotTick, data + 11, sizeof(uint32_t));
        deltas.clear();
        deltaOffsets.clear();
        hasNewDelta = false;
    }

    /** @brief Append a received delta (any type) to the unified delta buffer. */
    void receiveDelta(const uint8_t* data, size_t size) {
        uint32_t tick = 0;
        // tick is now at bytes 11-14 in the 15-byte header
        if (size >= 15) std::memcpy(&tick, data + 11, sizeof(uint32_t));
        deltaOffsets.push_back({tick, deltas.size()});
        deltas.insert(deltas.end(), data, data + size);
        hasNewDelta = true;
    }
};

} // namespace voxelmmo
