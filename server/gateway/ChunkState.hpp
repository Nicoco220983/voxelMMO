#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <utility>

namespace voxelmmo {

/**
 * @brief Serialised state cache for one chunk (gateway-side only).
 *
 * Stores a unified buffer containing:
 *   [snapshot][snapshot_delta_1][tick_delta_1][tick_delta_2]...
 *
 * Each entry in `entries` tracks: {tick, offset, length}
 *   - tick: server tick when this message was built
 *   - offset: byte offset in the unified buffer where message starts
 *   - length: byte length of this message
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
     * @brief Check if buffer contains only a snapshot (no deltas).
     * Assumes buffer is not empty (call isEmpty() first).
     */
    bool hasOnlySnapshot() const {
        return entries.size() == 1;
    }

    /**
     * @brief Get the number of delta messages appended after the snapshot.
     * @return Number of deltas (entries.size() - 1), or 0 if no entries.
     */
    size_t getDeltaCount() const {
        return entries.empty() ? 0 : entries.size() - 1;
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

    /**
     * @brief Receive a message (snapshot or delta) and update the unified buffer.
     *
     * Handles all server message types appropriately:
     * - CHUNK_SNAPSHOT (0, 1): Clears all existing data, stores as new snapshot
     * - CHUNK_SNAPSHOT_DELTA (2, 3): Clears previous deltas (keeps snapshot), appends delta
     * - CHUNK_TICK_DELTA (4, 5): Just appends to existing buffer
     *
     * @param data Pointer to message data (including 15-byte header).
     * @param size Total message size in bytes.
     */
    void receiveMessage(const uint8_t* data, size_t size) {
        if (size < 1) return;
        
        const uint8_t msgType = data[0];
        
        // Extract tick from header (bytes 11-14)
        uint32_t tick = 0;
        if (size >= 15) std::memcpy(&tick, data + 11, sizeof(uint32_t));
        
        // Check message type to determine handling
        const bool isSnapshot = (msgType == 0 || msgType == 1);  // CHUNK_SNAPSHOT[_COMPRESSED]
        const bool isSnapshotDelta = (msgType == 2 || msgType == 3);  // CHUNK_SNAPSHOT_DELTA[_COMPRESSED]
        
        if (isSnapshot) {
            // Full snapshot: clear everything and start fresh
            clear();
            buffer.assign(data, data + size);
            entries.push_back({tick, 0, size});
        } else if (isSnapshotDelta) {
            // Snapshot delta: supersedes all previous deltas, keep only snapshot
            if (!entries.empty()) {
                // Keep only the first entry (snapshot)
                const auto& snapshotEntry = entries[0];
                buffer.resize(snapshotEntry.length);
                entries.resize(1);
            }
            // Append the snapshot delta
            size_t offset = buffer.size();
            entries.push_back({tick, offset, size});
            buffer.insert(buffer.end(), data, data + size);
        } else {
            // Tick delta: just append
            size_t offset = buffer.size();
            entries.push_back({tick, offset, size});
            buffer.insert(buffer.end(), data, data + size);
        }
        
        hasNewData = true;
    }
};

} // namespace voxelmmo
