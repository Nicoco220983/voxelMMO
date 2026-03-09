#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <utility>

namespace voxelmmo {

/**
 * @brief Serialised state cache for one chunk.
 *
 * Stores a unified buffer containing:
 *   [snapshot][snapshot_delta_1][tick_delta_1][tick_delta_2]...
 *
 * Each entry in `entries` tracks: {tick, offset, length}
 *   - tick: server tick when this message was built
 *   - offset: byte offset in the unified buffer where message starts
 *   - length: byte length of this message
 *
 * Used symmetrically on both sides of the pipeline:
 *
 *   Game-engine side (Chunk::state):
 *     - Unified buffer built by Chunk::buildSnapshot() and build*Delta().
 *     - hasNewData: set when any message is appended, cleared by dispatch loop.
 *     - scratch: reusable staging buffer for compression.
 *
 *   Gateway side (StateManager):
 *     - Unified buffer populated by receiveSnapshot() and receiveDelta().
 *     - scratch is unused.
 *
 * Query logic given a recipient's lastReceivedTick T:
 *   if T == 0 or no entries exist with tick <= T:
 *       return entire buffer (new watcher - needs full history)
 *   else:
 *       return data from first entry with tick > T to end (catch-up deltas)
 *
 * Wire formats stored in the buffer:
 *
 *   snapshot (always CHUNK_SNAPSHOT_COMPRESSED):
 *     [0]       uint8   ServerMessageType = CHUNK_SNAPSHOT_COMPRESSED
 *     [1:3]     uint16  message size (LE)
 *     [3:11]    int64   ChunkId (LE)
 *     [11:15]   uint32  tick (LE)
 *     ...
 *
 *   deltas — appended after snapshot:
 *     [0]       uint8   ServerMessageType (CHUNK_SNAPSHOT_DELTA, CHUNK_TICK_DELTA, or *_COMPRESSED)
 *     [1:3]     uint16  message size (LE)
 *     [3:11]    int64   ChunkId (LE)
 *     [11:15]   uint32  tick (LE)
 *     ...
 */
struct ChunkState {
    // ── Unified buffer ────────────────────────────────────────────────────────

    /**
     * All serialized messages: snapshot first, then deltas appended.
     * Layout: [snapshot][delta_1][delta_2]...
     */
    std::vector<uint8_t> buffer;

    /**
     * Entry tracking each message in the buffer.
     * Invariant: entries[i].offset + entries[i].length == entries[i+1].offset
     *            (or == buffer.size() for the last entry)
     */
    struct Entry {
        uint32_t tick;      // Server tick when message was built
        size_t offset;      // Byte offset in buffer where message starts
        size_t length;      // Byte length of this message
    };
    std::vector<Entry> entries;

    /**
     * Set to true when new data is appended this tick.
     * Cleared by the serialize dispatch loop after batching.
     */
    bool hasNewData{false};

    /** Reusable scratch buffer (game-engine side only). */
    std::vector<uint8_t> scratch;

    // ── Query helpers ─────────────────────────────────────────────────────────

    /**
     * @brief Get pointer and length of data to send given recipient's last received tick.
     *
     * @param lastReceivedTick The tick the recipient last acknowledged (0 = new watcher).
     * @return pair of {data pointer, length}. If nothing to send, length is 0.
     *
     * Logic:
     *   - If entries is empty: returns {nullptr, 0}
     *   - If lastReceivedTick == 0 or no entry with tick <= lastReceivedTick exists:
     *       returns entire buffer (new watcher needs full state)
     *   - Else: returns data from first entry with tick > lastReceivedTick to end
     */
    std::pair<const uint8_t*, size_t> getDataToSend(uint32_t lastReceivedTick) const {
        if (entries.empty()) {
            return {nullptr, 0};
        }

        // New watcher (lastReceivedTick == 0) or no matching entry: send everything
        if (lastReceivedTick == 0) {
            return {buffer.data(), buffer.size()};
        }

        // Find first entry with tick > lastReceivedTick
        for (const auto& entry : entries) {
            if (entry.tick > lastReceivedTick) {
                return {buffer.data() + entry.offset, buffer.size() - entry.offset};
            }
        }

        // Recipient is up to date (all entries have tick <= lastReceivedTick)
        return {nullptr, 0};
    }

    /**
     * @brief Get the tick of the most recent entry (snapshot or delta).
     * @return Latest tick, or 0 if no entries exist.
     */
    uint32_t getLatestTick() const {
        if (entries.empty()) return 0;
        return entries.back().tick;
    }

    /**
     * @brief Check if buffer is empty (no serialization has been done).
     */
    bool isEmpty() const {
        return buffer.empty();
    }

    /**
     * @brief Clear all data (used when rebuilding snapshot).
     */
    void clear() {
        buffer.clear();
        entries.clear();
        hasNewData = false;
    }

    // ── Receive helpers (gateway side) ────────────────────────────────────────

    /** @brief Store a received full snapshot; discard all cached deltas. */
    void receiveSnapshot(const uint8_t* data, size_t size) {
        clear();
        buffer.assign(data, data + size);
        
        uint32_t tick = 0;
        // tick is at bytes 11-14 in the 15-byte header
        if (size >= 15) std::memcpy(&tick, data + 11, sizeof(uint32_t));
        
        entries.push_back({tick, 0, size});
        hasNewData = true;
    }

    /** @brief Append a received delta (any type) to the unified buffer. */
    void receiveDelta(const uint8_t* data, size_t size) {
        uint32_t tick = 0;
        // tick is at bytes 11-14 in the 15-byte header
        if (size >= 15) std::memcpy(&tick, data + 11, sizeof(uint32_t));
        
        size_t offset = buffer.size();
        entries.push_back({tick, offset, size});
        buffer.insert(buffer.end(), data, data + size);
        hasNewData = true;
    }
};

} // namespace voxelmmo
