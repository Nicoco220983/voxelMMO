#pragma once
#include "StateManager.hpp"
#include "common/Types.hpp"
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
 *   - Maintain StateManager so late-joining players can receive cached state.
 *
 * Threading note: receiveGameBatch() is called from the game-engine thread.
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
     * Batch wire format: direct concatenation of messages. Each message has a
     * [type(1)][size(2)] header (3 bytes) where size includes the header itself.
     * Safe to call from any thread; defers to the uWS event loop internally.
     *
     * @param data  Batch bytes (caller owns; copied immediately).
     * @param size  Total batch byte count.
     */
    void receiveGameBatch(const uint8_t* data, size_t size);

    /**
     * @brief Start listening for WebSocket connections. Blocks until stopped.
     * @param port  TCP port to bind to (e.g. 8080).
     */
    void listen(int port);

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
    void setPlayerDisconnectCallback(PlayerDisconnectCallback cb);

    /**
     * @brief Callback fired for every player input message.
     * Signature: (PlayerId, data*, size)
     */
    using PlayerInputCallback = std::function<void(PlayerId, const uint8_t*, size_t)>;
    void setPlayerInputCallback(PlayerInputCallback cb);

private:
    StateManager stateManager;

    uWS::App  wsApp;

    /**
     * @brief uWS event-loop pointer captured on the uWS thread during listen().
     * receiveGameBatch() may be called from any thread and uses this to defer
     * work safely onto the uWS event loop.
     */
    uWS::Loop* uwsLoop{nullptr};

    /** @brief Active connections keyed by PlayerId. */
    std::unordered_map<PlayerId, uWS::WebSocket<false, true, PlayerConnection>*> sockets;

    PlayerId nextPlayerId{1};

    PlayerConnectCallback    connectCb;
    PlayerDisconnectCallback disconnectCb;
    PlayerInputCallback      inputCb;

    /**
     * @brief Send a complete batch to all connected players.
     * Must be called from the uWS event loop thread.
     */
    void broadcastBatch(const uint8_t* data, size_t size);
};

} // namespace voxelmmo
