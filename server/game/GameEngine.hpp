#pragma once
#include "ChunkRegistry.hpp"
#include "WorldGenerator.hpp"
#include "game/systems/PhysicsSystem.hpp"
#include "game/systems/ChunkMembershipSystem.hpp"

#include "game/systems/SheepAISystem.hpp"
#include "game/entities/EntityFactory.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/InputComponent.hpp"
#include "common/Types.hpp"
#include "game/GatewayInfo.hpp"
#include "common/NetworkProtocol.hpp"
#include "game/WorldGenerator.hpp"
#include <entt/entt.hpp>
#include <unordered_map>
#include <set>
#include <memory>
#include <functional>
#include <mutex>
#include <random>
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
 *   2. tick() → stepPhysics → checkPlayersChunks → serializeChunks.
 *      Each chunk's updateState() decides snapshot/snapshot-delta/tick-delta.
 *   On new player join:
 *   3. JOIN message → pending queue → create in tick() → chunk activation.
 */
class GameEngine {
public:
    /**
     * @brief Construct a GameEngine with configurable world generation.
     * @param seed           World generation seed (0 = random if seedProvided is false).
     * @param seedProvided   Whether the seed was explicitly provided.
     * @param type           World generator type (NORMAL or TEST).
     * @param testEntityType Entity type for TEST mode (nullopt = no entity).
     */
    GameEngine(uint32_t seed = 0, bool seedProvided = false,
               GeneratorType type = GeneratorType::NORMAL,
               std::optional<EntityType> testEntityType = std::nullopt);

    // ── Gateway management ────────────────────────────────────────────────

    /** @brief Register a gateway. */
    void registerGateway(GatewayId gwId);

    /** @brief Unregister a gateway and remove all its players. */
    void unregisterGateway(GatewayId gwId);

    // ── Player management ─────────────────────────────────────────────────

    /**
     * @brief Register a new player (called on WebSocket connect).
     *
     * The entity is not yet spawned. The gateway should forward the client's
     * JOIN message to handlePlayerInput(), which will spawn the entity with
     * the requested EntityType and send the initial snapshot.
     */
    void registerPlayer(GatewayId gwId, PlayerId playerId);

    /** @brief Destroy a player entity and remove it from all chunk sets. */
    //void removePlayer(PlayerId playerId);

    /**
     * @brief Directly set a player's world position (sub-voxel coordinates).
     * Intended for test setup and administrative use.
     * No-op if the player does not exist.
     *
     * @param sx/sy/sz Position in sub-voxels (1 voxel = SUBVOXEL_SIZE units).
     */
    void teleportPlayer(PlayerId playerId, int32_t sx, int32_t sy, int32_t sz);

    // ── Entity Factory Access ─────────────────────────────────────────────

    /** @brief Get the entity factory (for queueing spawns). */
    EntityFactory* getEntityFactory() { return &entityFactory; }

    /**
     * @brief Create all pending entities queued in the entity factory.
     *
     * Call this at a precise step in the game loop to ensure all entities
     * are created at the same logical time.
     */
    void createPendingEntities();

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
     * @brief Callback invoked for player-specific messages (e.g., SELF_ENTITY).
     *
     * Unlike broadcast messages, these are sent to a single player only.
     * Signature: (PlayerId, data*, size)
     */
    using PlayerOutputCallback = std::function<void(PlayerId, const uint8_t*, size_t)>;

    void setPlayerOutputCallback(PlayerOutputCallback cb);

    // ── Per-player watched chunks callbacks ───────────────────────────────

    /**
     * @brief Callback invoked when a player's watched chunks change.
     *
     * Called during updateAndActivatePlayersWatchedChunks for each player.
     * GatewayEngine uses this to track which chunks each player receives.
     *
     * Signature: (PlayerId, const std::set<ChunkId>&)
     */
    using PlayerWatchedChunksCallback = std::function<void(PlayerId, const std::set<ChunkId>&)>;

    void setPlayerWatchedChunksCallback(PlayerWatchedChunksCallback cb);

    /**
     * @brief Callback invoked when a player needs initial chunk data.
     *
     * Called after a player entity is created (after JOIN).
     * GatewayEngine uses this to send full snapshots to the new player.
     *
     * Signature: (PlayerId, currentTick)
     */
    using SendInitialChunksCallback = std::function<void(PlayerId, uint32_t)>;

    void setSendInitialChunksCallback(SendInitialChunksCallback cb);

    /** @brief Access the world generator for terrain queries. */
    const WorldGenerator& getWorldGenerator() const { return worldGenerator; }
    
    /** @brief Access the world generator (non-const, for tests). */
    WorldGenerator& getWorldGenerator() { return worldGenerator; }

    // ── Test accessors ────────────────────────────────────────────────────

    /** @brief Access the ECS registry (for testing only). */
    entt::registry& getRegistry() { return registry; }

    /** @brief Access the chunk registry (for testing only). */
    ChunkRegistry& getChunkRegistry() { return chunkRegistry; }

    /** @brief Access the player entities map (for testing only). */
    const std::unordered_map<PlayerId, entt::entity>& getPlayerEntities() const { return playerEntities; }

    // ── Configuration ─────────────────────────────────────────────────────

    /** Number of chunk radii around a player position to activate (load). */
    static constexpr int32_t ACTIVATION_RADIUS = 1;
    /** Number of chunk radii around a player position to include in state messages. */
    static constexpr int32_t WATCH_RADIUS = 3;

private:
    entt::registry  registry;

    ChunkRegistry chunkRegistry;
    std::unordered_map<GatewayId, GatewayInfo>             gateways;
    std::unordered_map<PlayerId,  entt::entity>            playerEntities;

    int32_t  tickCount{0};

    OutputCallback outputCallback;
    PlayerOutputCallback playerOutputCallback;
    PlayerWatchedChunksCallback playerWatchedChunksCallback;
    SendInitialChunksCallback sendInitialChunksCallback;

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

    /** @brief Acquire a new unique global entity ID. */
    GlobalEntityId acquireEntityId() { return nextEntityId_++; }

    /** @brief Entity factory for deferred entity creation. */
    EntityFactory entityFactory;

    /** @brief Reused batch buffer — cleared and refilled every serialize call. */
    std::vector<uint8_t> batchBuf;
    
    /** @brief Entities marked for deletion that will be destroyed after serialization. */
    std::vector<entt::entity> pendingDeletions;
    
    /**
     * @brief Pending player creation request (enqueued at JOIN, processed in tick).
     */
    struct PendingPlayerCreation {
        PlayerId playerId;
        EntityType entityType;
    };
    
    /** @brief Queue of pending player entity creation requests. */
    std::vector<PendingPlayerCreation> pendingPlayerCreations_;
    
    /** @brief Stateless procedural terrain generator for world generation. */
    WorldGenerator worldGenerator;

    /** @brief Generate a deterministic random seed. */
    static uint32_t generateRandomSeed();

    /**
     * @brief Serialise all chunks by calling updateState() on each.
     *
     * Each chunk's updateState() will decide whether to build a snapshot,
     * snapshot delta, or tick delta based on its current state.
     * Then dispatches new deltas to all watching gateways.
     */
    void serializeChunks();

    /**
     * @brief Send SELF_ENTITY messages to newly created players.
     *
     * Finds entities with DirtyComponent::isCreated() and PlayerComponent,
     * builds SELF_ENTITY message for each, and sends via playerOutputCallback.
     * Called before serializeChunks() in tick().
     */
    void sendSelfEntityMessages();

    /**
     * @brief Clear all dirty flags after serialization.
     *
     * Clears snapshot and tick dirty flags for all entities, and voxel deltas
     * for all chunks. Called at the end of tick() after all serialization.
     */
    void clearAllDirtyFlags();

    /**
     * @brief Process pending player creation requests.
     *
     * Creates player entities directly (without EntityFactory) from the
     * pendingPlayerCreations_ queue. Called at the start of tick() after
     * createPendingEntities().
     */
    void processPendingPlayerCreations();

    /**
     * @brief Step physics simulation for all entities.
     */
    void stepPhysics();

    /**
     * @brief Get the chunk containing the given world position.
     */
    const Chunk* chunkAt(int32_t px, int32_t py, int32_t pz) noexcept;
};

} // namespace voxelmmo
