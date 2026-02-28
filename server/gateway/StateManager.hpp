#pragma once
#include "common/Types.hpp"
#include "common/ChunkState.hpp"
#include <unordered_map>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Manages per-chunk state received from the GameEngine on the gateway side.
 *
 * Each watched chunk keeps a ChunkState (snapshot + deltas).  When a new
 * player joins and needs a chunk, the gateway picks the best cached message
 * to send without waiting for the game engine to re-serialise.
 */
class StateManager {
public:
    /**
     * @brief Route an incoming chunk message to the correct ChunkState bucket.
     *
     * The message type is read from byte[0]; the ChunkId from bytes [1:9].
     *
     * @param data  Raw message bytes.
     * @param size  Byte count (minimum 9).
     */
    void receiveChunkMessage(const uint8_t* data, size_t size);

    /** @brief Get (or create) the ChunkState for a chunk. */
    ChunkState& getChunkState(ChunkId id);

    /** @brief Read-only access; returns nullptr if chunk is not tracked. */
    const ChunkState* findChunkState(ChunkId id) const;

    /** @brief Remove state for a chunk that is no longer watched. */
    void removeChunk(ChunkId id);

private:
    std::unordered_map<ChunkId, ChunkState> chunkStates;
};

} // namespace voxelmmo
