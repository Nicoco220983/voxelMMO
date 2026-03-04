#pragma once
#include <cstdint>

namespace voxelmmo {

/**
 * @brief First byte of every chunk state message.
 *
 * Odd values are the LZ4-compressed counterpart of the preceding even value.
 * The header (type byte + ChunkId) is always left uncompressed so the gateway
 * can route messages without decompressing them.
 */
enum class ChunkMessageType : uint8_t {
    SNAPSHOT                  = 0,
    SNAPSHOT_COMPRESSED       = 1,
    SNAPSHOT_DELTA            = 2,
    SNAPSHOT_DELTA_COMPRESSED = 3,
    TICK_DELTA                = 4,
    TICK_DELTA_COMPRESSED     = 5,
    SELF_ENTITY               = 6,  ///< Self-identification: type(1)+ChunkId(8)+tick(4)+GlobalEntityId(4) = 17 bytes
};

/** @brief Entity delta sub-type, encoded as the first byte of each entity record in a delta. */
enum class DeltaType : uint8_t {
    CREATE_ENTITY      = 0,  ///< Entity appears in this chunk (newly spawned or moved from elsewhere).
    UPDATE_ENTITY      = 1,  ///< Entity already known in this chunk; only dirty components are present.
    DELETE_ENTITY      = 2,  ///< Entity removed from this chunk (despawned or moved elsewhere).
    CHUNK_CHANGE_ENTITY = 3, ///< Entity moved to different chunk; old chunk sends this with new ChunkId.
};

/** @brief Registered entity types. */
enum class EntityType : uint8_t {
    PLAYER       = 0,  ///< Full-physics player (gravity + collision)
    GHOST_PLAYER = 1,  ///< Ghost player (noclip, no gravity)
    SHEEP        = 2,  ///< Passive mob: wanders randomly, blocked by voxels
};

/** @brief First byte of every client → server binary WebSocket frame. */
enum class ClientMessageType : uint8_t {
    INPUT = 0,  ///< uint8 buttons | float32 yaw | float32 pitch — 9-byte payload (total 10 bytes)
    JOIN  = 1,  ///< uint8 EntityType — 1-byte payload (total 2 bytes)
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
