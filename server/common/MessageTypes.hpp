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
};

/** @brief Entity delta sub-type, encoded as the first byte of each entity record in a delta. */
enum class DeltaType : uint8_t {
    NEW_ENTITY    = 0,  ///< First time this entity appears; serialize all non-default components.
    UPDATE_ENTITY = 1,  ///< Entity already known; only dirty components are present.
    DELETE_ENTITY = 2,  ///< Entity was removed from the simulation.
};

/** @brief Registered entity types. */
enum class EntityType : uint8_t {
    PLAYER       = 0,  ///< Full-physics player (gravity + collision)
    GHOST_PLAYER = 1,  ///< Ghost player (noclip, no gravity)
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
