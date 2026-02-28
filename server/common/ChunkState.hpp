#pragma once
#include <vector>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Serialised state cache for one chunk.
 *
 * Used symmetrically on both sides of the pipeline:
 *
 *   Game-engine side (Chunk::state):
 *     - snapshot / snapshotDelta / tickDeltas are written by Chunk::build*().
 *     - scratch is a reusable staging buffer for entity serialisation and
 *       in-place delta compression.
 *
 *   Gateway side (StateManager):
 *     - snapshot / snapshotDelta / tickDeltas are populated by receive*().
 *     - scratch is unused.
 *
 * Wire formats stored in each buffer:
 *
 *   snapshot  (always SNAPSHOT_COMPRESSED):
 *     [0]       uint8   ChunkMessageType = SNAPSHOT_COMPRESSED
 *     [1:9]     int64   ChunkId (LE)
 *     [9]       uint8   flags  — bit 0: entity section is LZ4 compressed
 *     [10:14]   int32   compressed_voxel_size (cvs)
 *     [14:14+cvs]       LZ4( voxels[CHUNK_VOXEL_COUNT] )
 *     [14+cvs:18+cvs]   int32   entity_section_stored_size (ess)
 *     if flags & 0x01:
 *       [18+cvs:22+cvs] int32   entity_uncompressed_size
 *       [22+cvs:…]              LZ4( entity_data )
 *     else:
 *       [18+cvs:18+cvs+ess]     raw entity_data  (int32 count + records)
 *
 *   snapshotDelta / tickDeltas[i]  (SNAPSHOT_DELTA or SNAPSHOT_DELTA_COMPRESSED etc.):
 *     [0]       uint8   ChunkMessageType
 *     [1:9]     int64   ChunkId (LE)
 *     if *_COMPRESSED:
 *       [9:13]  int32   uncompressed_payload_size
 *       [13:]           LZ4( raw_payload )
 *     else:
 *       [9:]            raw_payload:
 *                         int32  voxel_delta_count
 *                         (uint16 VoxelId, uint8 VoxelType) × count
 *                         int32  entity_delta_count
 *                         (uint8 DeltaType, …entity fields…) × count
 */
struct ChunkState {
    /** Latest full snapshot message (ready to send). */
    std::vector<uint8_t> snapshot;

    /**
     * Latest snapshot delta (ready to send).
     * Empty vector means nothing has changed since the last snapshot.
     */
    std::vector<uint8_t> snapshotDelta;

    /**
     * Tick deltas accumulated since the last snapshot or snapshot delta.
     * Front = oldest, back = newest.
     */
    std::vector<std::vector<uint8_t>> tickDeltas;

    /**
     * Reusable scratch buffer (game-engine side only).
     *   - During snapshot build: raw entity section before copy/compress.
     *   - During delta build:    temporary copy of raw payload before
     *                            in-place LZ4 compression.
     */
    std::vector<uint8_t> scratch;

    // ── Gateway-side receive helpers ──────────────────────────────────────

    /** @brief Store a received full snapshot; discard all cached deltas. */
    void receiveSnapshot(const uint8_t* data, size_t size) {
        snapshot.assign(data, data + size);
        snapshotDelta.clear();
        tickDeltas.clear();
    }

    /** @brief Store a received snapshot delta; discard tick deltas. */
    void receiveSnapshotDelta(const uint8_t* data, size_t size) {
        snapshotDelta.assign(data, data + size);
        tickDeltas.clear();
    }

    /** @brief Append a received tick delta. */
    void receiveTickDelta(const uint8_t* data, size_t size) {
        tickDeltas.emplace_back(data, data + size);
    }
};

} // namespace voxelmmo
