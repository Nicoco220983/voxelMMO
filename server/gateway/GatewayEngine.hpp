#pragma once
#include "common/Types.hpp"
#include "gateway/ChunkState.hpp"
#include "gateway/PlayerInfo.hpp"
#include <uwebsockets/App.h>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Per-WebSocket connection data stored inside µWebSockets' user-data slot.
 */
struct PlayerConnection {
    PlayerId playerId{0};
};

/**
 * @brief Gateway engine – accepts WebSocket clients and forwards chunk state.
 *
 * Responsibilities:
 *   - Accept / close WebSocket connections.
 *   - Forward serialised chunk state messages (received from GameEngine via IPC)
 *     to the appropriate connected players.
 *   - Forward raw player-input messages from clients to the GameEngine.
 *   - Maintain per-chunk state cache so late-joining players can receive cached state.
 *
 * Threading note: receiveGameMessage() is called from the game-engine thread.
 * uWebSockets' App::run() blocks the calling thread. Use uWS::Loop::defer() to
 * safely push work from another thread onto the uWS event loop.
 */
class GatewayEngine {
public:
    GatewayEngine();
    ~GatewayEngine();

    /**
     * @brief Deliver one tick's batch of serialised messages from the game engine.
     *
     * Batch wire format: concatenated chunk messages [msg1][msg2]...
     * Each message has a [type(1)][size(2)] header (3 bytes) where size includes the header itself.
     * Safe to call from any thread; defers to the uWS event loop internally.
     *
     * The gateway splits the concatenated buffer into individual messages
     * and routes each to the appropriate ChunkState bucket.
     *
     * @param data  Batch bytes (caller owns; copied immediately).
     * @param size  Total batch byte count.
     */
    void receiveGameMessage(const uint8_t* data, size_t size);

    /**
     * @brief Deliver a player-specific message from the game engine.
     *
     * Unlike receiveGameMessage(), this sends the message to a single player only.
     * Used for SELF_ENTITY messages and other player-specific notifications.
     * Safe to call from any thread; defers to the uWS event loop internally.
     *
     * @param playerId  Target player.
     * @param data      Message bytes (caller owns; copied immediately).
     * @param size      Message byte count.
     */
    void receiveGameMessageForPlayer(PlayerId playerId, const uint8_t* data, size_t size);

    /**
     * @brief Start listening for WebSocket connections. Blocks until stopped.
     * @param port  TCP port to bind to (e.g. 8080).
     */
    void listen(int port);

    /**
     * @brief Stop the gateway server and close all connections.
     *
     * This stops the uWS event loop, causing listen() to return.
     * Safe to call from any thread (e.g., signal handler).
     */
    void stop();

    /**
     * @brief Callback fired when a player connects (use to notify GameEngine).
     * Signature: (PlayerId)
     */
    using PlayerConnectCallback = std::function<void(PlayerId)>;
    void setPlayerConnectCallback(PlayerConnectCallback cb);

    /**
     * @brief Callback fired when a player disconnects.
     * Signature: (PlayerId)
     */
    using PlayerDisconnectCallback = std::function<void(PlayerId)>;
    void setPlayerDisconnectCallback(PlayerConnectCallback cb);

    /**
     * @brief Callback fired for every player input message.
     * Signature: (PlayerId, data*, size)
     */
    using PlayerInputCallback = std::function<void(PlayerId, const uint8_t*, size_t)>;
    void setPlayerInputCallback(PlayerInputCallback cb);

    // ── Per-chunk state management ─────────────────────────────────────────

    /**
     * @brief Route an incoming chunk message to the correct ChunkState bucket.
     *
     * Calls receiveMessage() which handles all message types appropriately:
     * - Snapshots: clear all existing data
     * - Snapshot deltas: clear previous deltas (keep snapshot), append delta  
     * - Tick deltas: just append to existing buffer
     *
     * The message type is read from byte[0]; the ChunkId from bytes [3:10].
     *
     * @param data  Raw message bytes.
     * @param size  Byte count (minimum 15 — header size).
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
    uWS::App  wsApp;

    /**
     * @brief uWS event-loop pointer captured on the uWS thread during listen().
     * receiveGameMessage() may be called from any thread and uses this to defer
     * work safely onto the uWS event loop.
     */
    uWS::Loop* uwsLoop{nullptr};

    /** @brief Listen socket for graceful shutdown. */
    us_listen_socket_t* listenSocket_{nullptr};

    /** @brief Active connections keyed by PlayerId. */
    std::unordered_map<PlayerId, uWS::WebSocket<false, true, PlayerConnection>*> sockets;

    PlayerId nextPlayerId{1};

    PlayerConnectCallback    connectCb;
    PlayerDisconnectCallback disconnectCb;
    PlayerInputCallback      inputCb;

    /** @brief Cached chunk states keyed by ChunkId. */
    std::unordered_map<ChunkId, ChunkState> chunkStates;

    /** @brief Per-player metadata keyed by PlayerId. */
    std::unordered_map<PlayerId, PlayerInfo> players;

    /**
     * @brief Send a complete batch to all connected players.
     * Must be called from the uWS event loop thread.
     */
    void broadcastBatch(const uint8_t* data, size_t size);

    /**
     * @brief Send a message to a specific player.
     * Must be called from the uWS event loop thread.
     * @return true if player was connected and message was sent.
     */
    bool sendToPlayer(PlayerId playerId, const uint8_t* data, size_t size);
};

} // namespace voxelmmo
