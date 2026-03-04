#pragma once
#include "Chunk.hpp"
#include "game/systems/PhysicsSystem.hpp"
#include "game/systems/ChunkMembershipSystem.hpp"
#include "game/systems/EntityStateSystem.hpp"
#include "game/systems/SheepAISystem.hpp"
#include "game/entities/EntityFactory.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/InputComponent.hpp"
#include "common/Types.hpp"
#include "common/GatewayInfo.hpp"
#include "common/NetworkProtocol.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <set>
#include <memory>
#include <functional>
#include <mutex>
#include <cstdint>

namespace voxelmmo {



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
     * @brief Queue a pending player entry (called on WebSocket connect).
     *
     * The entity is not yet spawned.  The gateway should forward the client's
     * JOIN message to handlePlayerInput(), which will spawn the entity with
     * the requested EntityType and send the initial snapshot.
     *
     * @param sx/sy/sz Spawn position in sub-voxels (1 voxel = SUBVOXEL_SIZE units).
     */
    void queuePendingPlayer(GatewayId gwId, PlayerId playerId,
                            int32_t sx, int32_t sy, int32_t sz);

    /**
     * @brief Spawn a new player entity with the given type.
     *
     * Delegates to playerFactories[type].  Default type is GHOST_PLAYER for
     * backward compatibility with tests that call addPlayer() directly.
     *
     * @param gwId     Gateway the player connected through.
     * @param playerId Persistent player identifier.
     * @param sx/sy/sz Spawn position in sub-voxels (1 voxel = SUBVOXEL_SIZE units).
     * @param type     Entity type (selects physics mode).
     */
    void addPlayer(GatewayId gwId, PlayerId playerId,
                   int32_t sx, int32_t sy, int32_t sz,
                   EntityType type = EntityType::GHOST_PLAYER);

    /** @brief Destroy a player entity and remove it from all chunk sets. */
    void removePlayer(PlayerId playerId);

    /**
     * @brief Directly set a player's world position (sub-voxel coordinates).
     * Intended for test setup and administrative use.
     * No-op if the player does not exist.
     *
     * @param sx/sy/sz Position in sub-voxels (1 voxel = SUBVOXEL_SIZE units).
     */
    void teleportPlayer(PlayerId playerId, int32_t sx, int32_t sy, int32_t sz);

    // ── Main loop ─────────────────────────────────────────────────────────

    /** @brief Advance one game tick (physics → chunk checks → tick-delta serialisation). */
    void tick();

    /**
     * @brief Process a raw client message (may be called from the gateway thread).
     *
     * Wire format: uint8 ClientMessageType | payload.
     *   INPUT (0x00): uint8 buttons | float32 yaw | float32 pitch — 9-byte payload (total 10 bytes).
     *   JOIN  (0x01): uint8 EntityType                            —  1-byte payload (total  2 bytes).
     *
     * @param playerId  Player whose entity to update.
     * @param data      Raw message bytes.
     * @param size      Must be ≥ 1.
     */
    void handlePlayerInput(PlayerId playerId, const uint8_t* data, size_t size);

    // ── Serialisation callbacks ───────────────────────────────────────────

    /**
     * @brief Callback invoked once per tick per gateway with a full batch of
     * serialised messages ready to be forwarded to clients.
     *
     * Batch wire format: repeated [ uint32 msgLen (LE) | msgLen bytes ]
     * The data pointer is valid only for the duration of the call.
     *
     * Signature: (GatewayId, data*, size)
     * Designed to map directly to writev(fd, …) when IPC moves to a real socket.
     */
    using OutputCallback = std::function<void(GatewayId, const uint8_t*, size_t)>;

    void setOutputCallback(OutputCallback cb);

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

    std::unordered_map<ChunkId,   std::unique_ptr<Chunk>>  chunks;
    std::unordered_map<GatewayId, GatewayInfo>             gateways;
    std::unordered_map<PlayerId,  entt::entity>            playerEntities;

    struct PendingPlayer { GatewayId gwId; int32_t sx, sy, sz; };
    std::unordered_map<PlayerId, PendingPlayer>            pendingPlayers;

    int32_t  tickCount{0};

    OutputCallback outputCallback;

    /**
     * @brief Serialises all public API access between the game-loop thread and
     * the gateway (uWS) thread.  Every public method that reads or writes game
     * state must hold this mutex for its entire duration.
     *
     * Recursive because unregisterGateway() → removePlayer() both need the lock.
     */
    std::recursive_mutex mtx_;

    /** @brief Monotonically increasing global entity ID counter (starts at 1, 0 reserved). */
    GlobalEntityId nextEntityId_{1};

    // TODO: remove it when migrated to sockets using writev
    /** @brief Acquire a new unique global entity ID. */
    GlobalEntityId acquireEntityId() { return nextEntityId_++; }

    /** @brief Reused batch buffer — cleared and refilled every serialize call. */
    std::vector<uint8_t> batchBuf;
    
    /** @brief Entities marked for deletion that will be destroyed after serialization. */
    std::vector<entt::entity> pendingDeletions;

    // ── Internal helpers ──────────────────────────────────────────────────

    Chunk& getOrActivateChunk(ChunkId id);

    /** @brief Return the chunk containing sub-voxel position (px, py, pz), or nullptr if not loaded. */
    Chunk* chunkAt(int32_t px, int32_t py, int32_t pz) noexcept;


    void   serializeSnapshot(GatewayId gwId);
    void   serializeSnapshotDelta();
    void   serializeTickDelta();
    void   stepPhysics();
};

} // namespace voxelmmo
