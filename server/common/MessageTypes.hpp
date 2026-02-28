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
    PLAYER = 0,
    // Add more types here; update client types.ts accordingly.
};

} // namespace voxelmmo
