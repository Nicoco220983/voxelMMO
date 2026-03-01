#pragma once
#include "common/Types.hpp"
#include "common/ChunkState.hpp"
#include <unordered_map>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Manages per-chunk state received from the GameEngine on the gateway side.
 *
 * Each watched chunk keeps a ChunkState (snapshot + unified delta buffer).
 * When a new player joins and needs a chunk, the gateway picks the best cached
 * message to send without waiting for the game engine to re-serialise.
 *
 * Per-player snapshot tick tracking lets the gateway skip resending a snapshot
 * that the player has already received.
 */
class StateManager {
public:
    /**
     * @brief Route an incoming chunk message to the correct ChunkState bucket.
     *
     * Snapshots call receiveSnapshot() (clears deltas); all delta variants call
     * receiveDelta() (appends to the unified delta buffer).
     * The message type is read from byte[0]; the ChunkId from bytes [1:9].
     *
     * @param data  Raw message bytes.
     * @param size  Byte count (minimum 13 — header size).
     */
    void receiveChunkMessage(const uint8_t* data, size_t size);

    /** @brief Get (or create) the ChunkState for a chunk. */
    ChunkState& getChunkState(ChunkId id);

    /** @brief Read-only access; returns nullptr if chunk is not tracked. */
    const ChunkState* findChunkState(ChunkId id) const;

    /** @brief Remove state for a chunk that is no longer watched. */
    void removeChunk(ChunkId id);

    // ── Per-player snapshot tick tracking ─────────────────────────────────
    // Records which snapshot tick each player currently holds for each chunk.
    // Used to decide whether to send a full snapshot or just accumulated deltas
    // when a player starts watching a chunk.

    /** @brief Record that player @p pid has state up to @p tick for @p cid. */
    void setPlayerStateTick(PlayerId pid, ChunkId cid, uint32_t tick);

    /**
     * @brief Latest state tick @p pid holds for @p cid (snapshot or delta).
     * @return 0 if the player has not yet received any state for that chunk.
     */
    uint32_t getPlayerStateTick(PlayerId pid, ChunkId cid) const;

    /** @brief Remove all per-chunk tracking for a disconnected player. */
    void removePlayer(PlayerId pid);

private:
    std::unordered_map<ChunkId, ChunkState> chunkStates;

    /** pid → (ChunkId → last state tick received, from snapshot or delta). */
    std::unordered_map<PlayerId, std::unordered_map<ChunkId, uint32_t>> playerStateTick;
};

} // namespace voxelmmo
