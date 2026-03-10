#pragma once
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Server message type - first byte of every server → client network message.
 *
 * All server messages start with [type][size] header (3 bytes total):
 *   - type: ServerMessageType (uint8)
 *   - size: uint16 LE (total message size including header)
 *
 * Chunk state messages (types 0-5) are followed by [chunk_id(8)][tick(4)] (12 bytes).
 *
 * Odd values are the LZ4-compressed counterpart of the preceding even value.
 * The header (type + size + ChunkId + tick) is always left uncompressed
 * so the gateway can route messages without decompressing them.
 */
enum class ServerMessageType : uint8_t {
    CHUNK_SNAPSHOT                  = 0,  ///< Full chunk state (voxels + entities)
    CHUNK_SNAPSHOT_COMPRESSED       = 1,  ///< LZ4-compressed snapshot
    CHUNK_SNAPSHOT_DELTA            = 2,  ///< Delta since last snapshot
    CHUNK_SNAPSHOT_DELTA_COMPRESSED = 3,  ///< LZ4-compressed snapshot delta
    CHUNK_TICK_DELTA                = 4,  ///< Per-tick delta
    CHUNK_TICK_DELTA_COMPRESSED     = 5,  ///< LZ4-compressed tick delta
    SELF_ENTITY                     = 6,  ///< Self-identification with GlobalEntityId
};

/** @brief Entity delta sub-type, encoded as bit flags in the first byte of each entity record in a delta. */
enum class DeltaType : uint8_t {
    CREATE_ENTITY       = 1 << 0,  ///< Entity appears in this chunk (newly spawned or moved from elsewhere).
    UPDATE_ENTITY       = 1 << 1,  ///< Entity already known in this chunk; only dirty components are present.
    DELETE_ENTITY       = 1 << 2,  ///< Entity removed from this chunk (despawned or moved elsewhere).
    CHUNK_CHANGE_ENTITY = 1 << 3,  ///< Entity moved to different chunk; old chunk sends this with new ChunkId.
};

/** @brief First byte of every client → server binary WebSocket frame. */
enum class ClientMessageType : uint8_t {
    INPUT = 0,  ///< buttons uint8 + yaw float32 + pitch float32 — 9 bytes payload (total 13 with header)
    JOIN  = 1,  ///< EntityType uint8 — 1 byte payload (total 5 with header)
};

/** @brief Bitmask flags for the INPUT message buttons field. */
enum class InputButton : uint8_t {
    FORWARD  = 1 << 0,
    BACKWARD = 1 << 1,
    LEFT     = 1 << 2,
    RIGHT    = 1 << 3,
    JUMP     = 1 << 4,
    DESCEND  = 1 << 5,
};

} // namespace voxelmmo
