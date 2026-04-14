#pragma once

#include "common/Types.hpp"
#include "game/GatewayInfo.hpp"
#include "game/ChunkRegistry.hpp"
#include "game/ChunkSerializer.hpp"
#include "game/entities/EntityFactory.hpp"
#include <entt/entity/fwd.hpp>
#include <entt/entity/registry.hpp>
#include <functional>
#include <unordered_map>
#include <set>
#include <vector>
#include <cstdint>
#include <string>
#include <atomic>
#include <mutex>
#include <memory>
#include <optional>

namespace voxelmmo {

// Forward declarations
class Chunk;
class SaveSystem;
class WorldGenerator;

enum class EntityType : uint8_t;
enum class GeneratorType : uint8_t;

/**
 * @brief Authoritative game server.
 *
 * Owns the ECS registry, all live Chunk objects, and the main game loop.
 * Serialises watched chunks per-gateway, with snapshot/delta based on what
 * each gateway has already received.
 *
 * Typical call sequence per tick:
 *   1. Receive player inputs → modify ECS components via Component::modify().
 *   2. tick() → stepPhysics → checkPlayersChunks → serializeChunks.
 *      For each gateway: serialize only watched chunks, dispatch to that gateway.
 *   On new player join:
 *   3. JOIN message → pending queue → create in tick() → chunk activation.
 */
class GameEngine {
public:
    /**
     * @brief Construct a GameEngine.
     * Creates internal SaveSystem, loads/creates global state, and configures WorldGenerator.
     * @param cliSeed        Seed from CLI (used if no saved state exists).
     * @param cliType        Generator type from CLI.
     * @param seedProvided   Whether the seed was explicitly provided by CLI.
     * @param testEntityType Entity type for TEST mode (nullopt = no entity).
     * @param gameKey        Optional save directory name (default: SaveSystem::DEFAULT_GAME_KEY).
     */
    GameEngine(uint32_t cliSeed, GeneratorType cliType, bool seedProvided,
               std::optional<EntityType> testEntityType = std::nullopt,
               const std::string& gameKey = std::string("voxelmmo_default"));

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

    /**
     * @brief Mark a player entity for delayed deletion after disconnect.
     *
     * The player entity will be kept alive for DISCONNECT_GRACE_PERIOD_TICKS
     * (60 ticks = 60 seconds) to allow for reconnection. If the player
     * reconnects before the grace period expires, call cancelPlayerDisconnection()
     * or the component will be automatically removed.
     *
     * @param playerId The player ID to mark for disconnection.
     */
    void markPlayerDisconnected(PlayerId playerId);

    /**
     * @brief Cancel a pending disconnection (player reconnected in time).
     *
     * Removes the DisconnectedPlayerComponent from the entity to keep it alive.
     *
     * @param playerId The player ID that reconnected.
     * @return true if a pending disconnection was cancelled, false otherwise.
     */
    bool cancelPlayerDisconnection(PlayerId playerId);

    /**
     * @brief Directly set a player's world position (sub-voxel coordinates).
     * Intended for test setup and administrative use.
     * No-op if the player does not exist.
     *
     * @param sx/sy/sz Position in sub-voxels (1 voxel = SUBVOXEL_SIZE units).
     */
    void teleportPlayer(PlayerId playerId, SubVoxelCoord sx, SubVoxelCoord sy, SubVoxelCoord sz);

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

    /** @brief Advance one game tick (physics → chunk checks → serialisation). */
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
     * Batch wire format: concatenated chunk messages [msg1][msg2]...
     * Each message has a [type(1)][size(2)] header.
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

    /**
     * @brief Force-send a full snapshot for all watched chunks of a gateway.
     * Use when a player first joins, after a reconnect, or on acknowledgement.
     * @param gwId  Target gateway.
     */
    void sendSnapshot(GatewayId gwId);

    /** @brief Access the world generator for terrain queries. */
    const WorldGenerator& getWorldGenerator() const;
    
    /** @brief Access the world generator (non-const, for tests). */
    WorldGenerator& getWorldGenerator();
    
    /** @brief Save all active chunks (for shutdown). */
    void saveActiveChunks();
    
    /** @brief Save global state to disk. */
    void saveGlobalState();
    
    /** @brief Get the SaveSystem (may be nullptr if not initialized). */
    SaveSystem* getSaveSystem() const;
    
    /** @brief Get the base save directory (empty string if not initialized). */
    std::string getSaveDirectory() const;
    
    /** @brief Get the seed from SaveSystem (0 if not initialized). */
    uint32_t getSeed() const;
    
    /** @brief Get the generator type from SaveSystem (NORMAL if not initialized). */
    GeneratorType getGeneratorType() const;

    // ── Game loop lifecycle ────────────────────────────────────────────────

    /**
     * @brief Start the game loop in the current thread.
     *
     * Blocks until stop() is called. Runs at TICK_RATE ticks per second.
     */
    void run();

    /**
     * @brief Request graceful shutdown of the game loop.
     *
     * Signals the game loop to stop. The loop will complete the current
     * tick, save active chunks, and then return.
     */
    void stop();

    /**
     * @brief Check if the game loop is currently running.
     */
    bool isRunning() const { return running_.load(); }

    // ── Test accessors ────────────────────────────────────────────────────

    /** @brief Access the ECS registry (for testing only). */
    entt::registry& getRegistry() { return registry; }

    /** @brief Access the chunk registry (for testing only). */
    ChunkRegistry& getChunkRegistry() { return chunkRegistry; }

    /** @brief Access the player entities map (for testing only). */
    const std::unordered_map<PlayerId, entt::entity>& getPlayerEntities() const { return playerEntities; }

    /** @brief Get current tick count (for testing only). */
    int32_t getTickCount() const { return tickCount; }

    // ── Configuration ─────────────────────────────────────────────────────

    /** Number of chunk radii around a player position to activate (load). */
    static constexpr int32_t ACTIVATION_RADIUS = 1;
    /** Number of chunk radii around a player position to include in state messages. */
    static constexpr int32_t WATCH_RADIUS = 3;

private:
    entt::registry registry;

    ChunkRegistry chunkRegistry;
    std::unordered_map<GatewayId, GatewayInfo> gateways;
    std::unordered_map<PlayerId, entt::entity> playerEntities;

    int32_t tickCount{0};
    uint32_t lastDisconnectCheckTick{0};  ///< Last tick when disconnect system was run

    OutputCallback outputCallback;
    PlayerOutputCallback playerOutputCallback;

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

    /** @brief Entity factory for deferred entity creation. */
    EntityFactory entityFactory;

    /** @brief Chunk serializer - handles serialization of all chunks. */
    ChunkSerializer serializer_;
    
    /** @brief Entities marked for deletion that will be destroyed after serialization. */
    std::vector<entt::entity> pendingDeletions_;
    
    /** @brief Pending player creation request (enqueued at JOIN, processed in tick). */
    struct PendingPlayerCreation {
        PlayerId playerId;
        EntityType entityType;
    };
    std::vector<PendingPlayerCreation> pendingPlayerCreations_;
    
    /** @brief Pending voxel deletion request (enqueued on VOXEL_DESTROY message). */
    struct PendingVoxelDeletion {
        int32_t vx;
        int32_t vy;
        int32_t vz;
    };
    std::vector<PendingVoxelDeletion> pendingVoxelDeletions_;
    
    /** @brief Pending voxel creation request (enqueued on VOXEL_CREATE message). */
    struct PendingVoxelCreation {
        int32_t vx;
        int32_t vy;
        int32_t vz;
        VoxelType voxelType;
    };
    std::vector<PendingVoxelCreation> pendingVoxelCreations_;
    
    /** @brief Pending bulk voxel deletion request (enqueued on BULK_VOXEL_DESTROY message). */
    struct PendingBulkVoxelDeletion {
        int32_t startX;
        int32_t startY;
        int32_t startZ;
        int32_t endX;
        int32_t endY;
        int32_t endZ;
    };
    std::vector<PendingBulkVoxelDeletion> pendingBulkVoxelDeletions_;
    
    /** @brief Pending bulk voxel creation request (enqueued on BULK_VOXEL_CREATE message). */
    struct PendingBulkVoxelCreation {
        int32_t startX;
        int32_t startY;
        int32_t startZ;
        int32_t endX;
        int32_t endY;
        int32_t endZ;
        VoxelType voxelType;
    };
    std::vector<PendingBulkVoxelCreation> pendingBulkVoxelCreations_;

    /** @brief Pending tool use request (enqueued on TOOL_USE input). */
    struct PendingToolUse {
        PlayerId playerId;
        uint8_t toolId;
        float yaw;
        float pitch;
    };
    std::vector<PendingToolUse> pendingToolUses_;

    /** @brief Pending tool selection request (enqueued on TOOL_SELECT input). */
    struct PendingToolSelect {
        PlayerId playerId;
        uint8_t toolId;
    };
    std::vector<PendingToolSelect> pendingToolSelects_;
    
    /** @brief Stateless procedural terrain generator for world generation. */
    std::unique_ptr<WorldGenerator> worldGenerator;
    
    /** @brief SaveSystem for loading/saving chunks (must be initialized before run()). */
    std::unique_ptr<SaveSystem> saveSystem_;

    // ── Game loop control ───────────────────────────────────────────────────

    /** @brief Flag to control game loop execution. */
    std::atomic<bool> running_{false};
    
    /** @brief Flag to request shutdown. */
    std::atomic<bool> stopRequested_{false};

    // Private methods (declared here, defined in cpp)
    GlobalEntityId acquireEntityId() { return nextEntityId_++; }
    void serializeChunks();
    void processPendingDeletions(uint32_t tick);
    void processPendingPlayerCreations();
    void processPendingVoxelDeletions();
    void processPendingVoxelCreations();
    void processPendingBulkVoxelDeletions();
    void processPendingBulkVoxelCreations();
    void processPendingToolSelects();
    void processPendingToolUses();
    const Chunk* chunkAt(SubVoxelCoord px, SubVoxelCoord py, SubVoxelCoord pz) noexcept;
    uint32_t generateRandomSeed();
};

} // namespace voxelmmo
