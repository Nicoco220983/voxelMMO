#pragma once
#include "Chunk.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <set>
#include <memory>
#include <functional>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Metadata the GameEngine tracks for each connected gateway.
 */
struct GatewayInfo {
    std::set<PlayerId> players;      ///< Players routed through this gateway.
    std::set<ChunkId>  watchedChunks; ///< Chunks the gateway currently needs state for.
};

/**
 * @brief Authoritative game server.
 *
 * Owns the ECS registry, all live Chunk objects, and the main game loop.
 * Serialised chunk messages are delivered via a callback so the transport
 * layer (same-process queue, Unix socket, or TCP) stays decoupled.
 *
 * Typical call sequence per tick:
 *   1. Receive player inputs → modify ECS components via Component::modify().
 *   2. tick() → stepPhysics → checkPlayersChunks → serializeTickDelta.
 *   Every N ticks:
 *   3. serializeSnapshotDelta() – clears snapshot dirty flags.
 *   On new player join:
 *   4. addPlayer() → serializeSnapshot() for all chunks in watch radius.
 */
class GameEngine {
public:
    GameEngine();

    // ── Gateway management ────────────────────────────────────────────────

    /** @brief Register a gateway. Must be called before addPlayer(). */
    void registerGateway(GatewayId gwId);

    /** @brief Unregister a gateway and remove all its players. */
    void unregisterGateway(GatewayId gwId);

    // ── Player management ─────────────────────────────────────────────────

    /**
     * @brief Spawn a new player entity.
     * @param gwId     Gateway the player connected through.
     * @param playerId Persistent player identifier.
     * @param sx/sy/sz Spawn world coordinates.
     * @return Assigned EntityId.
     */
    EntityId addPlayer(GatewayId gwId, PlayerId playerId,
                       float sx, float sy, float sz);

    /** @brief Destroy a player entity and remove it from all chunk sets. */
    void removePlayer(PlayerId playerId);

    // ── Main loop ─────────────────────────────────────────────────────────

    /** @brief Advance one game tick (physics → chunk checks → tick-delta serialisation). */
    void tick();

    // ── Serialisation callbacks ───────────────────────────────────────────

    /**
     * @brief Callback invoked with every serialised chunk state message.
     *
     * Signature: (GatewayId, ChunkId, data*, size)
     * The data pointer is valid only for the duration of the call.
     */
    using ChunkMessageCallback =
        std::function<void(GatewayId, ChunkId, const uint8_t*, size_t)>;

    void setChunkMessageCallback(ChunkMessageCallback cb);

    /**
     * @brief Force-send a full snapshot for all watched chunks of a gateway.
     * Use when a player first joins, after a reconnect, or on acknowledgement.
     * @param gwId  Target gateway.
     */
    void sendSnapshot(GatewayId gwId);

    // ── Configuration ─────────────────────────────────────────────────────

    /** Number of chunk radii around a player position to activate (load). */
    static constexpr int32_t ACTIVATION_RADIUS = 2;
    /** Number of chunk radii around a player position to include in state messages. */
    static constexpr int32_t WATCH_RADIUS = 3;
    /** Ticks between full snapshot-delta sends. */
    static constexpr int32_t SNAPSHOT_DELTA_INTERVAL = 10;

private:
    entt::registry  registry;

    std::unordered_map<ChunkId,   std::unique_ptr<Chunk>>         chunks;
    std::unordered_map<GatewayId, GatewayInfo>                    gateways;
    std::unordered_map<PlayerId,  entt::entity>                   playerEntities;
    std::unordered_map<EntityId,  std::unique_ptr<BaseEntity>> entityMap;

    EntityId nextEntityId{1};
    int32_t  tickCount{0};

    ChunkMessageCallback chunkMsgCallback;

    // ── Internal helpers ──────────────────────────────────────────────────

    Chunk& activateChunk(ChunkId id);
    void   checkPlayersChunks();
    void   serializeSnapshot(GatewayId gwId);
    void   serializeSnapshotDelta();
    void   serializeTickDelta();
    void   stepPhysics(float dt);
};

} // namespace voxelmmo
