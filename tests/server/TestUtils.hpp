#pragma once
/**
 * @file TestUtils.hpp
 * @brief Test utilities for voxelMMO server tests.
 * 
 * Provides TestEnv - a simplified environment for writing game engine tests.
 * All operations are synchronous and single-threaded for determinism.
 */

#include <catch2/catch_test_macros.hpp>
#include "game/GameEngine.hpp"
#include "game/Chunk.hpp"
#include "game/ChunkRegistry.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/InputComponent.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/ChunkMembershipComponent.hpp"
#include "common/MessageTypes.hpp"
#include "common/NetworkProtocol.hpp"

#include <unordered_map>
#include <vector>
#include <functional>
#include <string>
#include <filesystem>

namespace voxelmmo {

/**
 * @brief Simplified test environment for game engine tests.
 * 
 * TestEnv wraps GameEngine and provides synchronous, deterministic
 * test helpers. It captures all network output for inspection.
 * 
 * Example usage:
 * @code
 * TEST_CASE("Player can walk") {
 *     TestEnv env(12345);  // Fixed seed
 *     PlayerId pid = env.addPlayer(EntityType::PLAYER);
 *     env.tick(10);  // Land on ground
 *     
 *     env.pressButton(pid, InputButton::FORWARD);
 *     env.tick(20);
 *     
 *     auto pos = env.getPosition(pid);
 *     REQUIRE(pos->z < 0);  // Moved forward
 * }
 * @endcode
 */
class TestEnv {
public:
    /**
     * @brief Create a test environment with optional fixed seed.
     * @param seed World generation seed (0 = random)
     */
    explicit TestEnv(uint32_t seed = 12345);
    
    // ── Player Management ────────────────────────────────────────────────────
    
    /**
     * @brief Add a new player to the game.
     * @param type Entity type (GHOST_PLAYER or PLAYER)
     * @return PlayerId for the new player
     * 
     * The player is spawned at the world generator's spawn position.
     * You must call tick() after this for the entity to be created.
     */
    PlayerId addPlayer(EntityType type = EntityType::GHOST_PLAYER);
    
    /**
     * @brief Remove a player from the game.
     * @param pid Player to remove
     */
    //void removePlayer(PlayerId pid);
    
    /**
     * @brief Teleport a player to a specific position.
     * @param pid Player to teleport
     * @param x,y,z Position in sub-voxels
     */
    void teleport(PlayerId pid, int32_t x, int32_t y, int32_t z);
    
    // ── Input Simulation ─────────────────────────────────────────────────────
    
    /**
     * @brief Set the complete input state for a player.
     * @param pid Player to control
     * @param buttons Button bitmask (InputButton flags)
     * @param yaw Camera yaw in radians (default 0)
     * @param pitch Camera pitch in radians (default 0, ghost only)
     */
    void setInput(PlayerId pid, uint8_t buttons, float yaw = 0.0f, float pitch = 0.0f);
    
    /**
     * @brief Press a button for a player.
     * @param pid Player to control
     * @param btn Button to press
     */
    void pressButton(PlayerId pid, InputButton btn);
    
    /**
     * @brief Release a button for a player.
     * @param pid Player to control
     * @param btn Button to release
     */
    void releaseButton(PlayerId pid, InputButton btn);
    
    /**
     * @brief Set the camera direction for a player.
     * @param pid Player to control
     * @param yaw Camera yaw in radians
     * @param pitch Camera pitch in radians (ghost only)
     */
    void setLook(PlayerId pid, float yaw, float pitch = 0.0f);
    
    // ── Entity Access ────────────────────────────────────────────────────────
    
    /**
     * @brief Get the entity handle for a player.
     * @param pid Player to look up
     * @return Entity handle, or entt::null if not found
     */
    entt::entity getEntity(PlayerId pid);
    
    /**
     * @brief Get position component for a player.
     * @param pid Player to look up
     * @return Pointer to position component, or nullptr
     */
    DynamicPositionComponent* getPosition(PlayerId pid);
    
    /**
     * @brief Get input component for a player.
     * @param pid Player to look up
     * @return Pointer to input component, or nullptr
     */
    InputComponent* getInput(PlayerId pid);
    
    /**
     * @brief Check if a player exists.
     * @param pid Player to check
     * @return true if player exists
     */
    bool hasPlayer(PlayerId pid);
    
    // ── Chunk Access ─────────────────────────────────────────────────────────
    
    /**
     * @brief Get a chunk by coordinates.
     * @param cx,cy,cz Chunk coordinates
     * @return Pointer to chunk, or nullptr if not loaded
     */
    Chunk* getChunk(int32_t cx, int32_t cy, int32_t cz);
    
    /**
     * @brief Check if a chunk exists.
     * @param cx,cy,cz Chunk coordinates
     * @return true if chunk is loaded
     */
    bool hasChunk(int32_t cx, int32_t cy, int32_t cz);
    
    /**
     * @brief Get chunk containing a world position.
     * @param x,y,z World position in sub-voxels
     * @return Pointer to chunk, or nullptr if not loaded
     */
    Chunk* getChunkAt(int32_t x, int32_t y, int32_t z);
    
    // ── Game Loop ────────────────────────────────────────────────────────────
    
    /**
     * @brief Advance the game simulation by N ticks.
     * @param n Number of ticks to advance (default 1)
     */
    void tick(int n = 1);
    
    /**
     * @brief Get the current tick count.
     * @return Current tick number
     */
    int getTickCount() const { return tickCount_; }
    
    // ── Output Capture ───────────────────────────────────────────────────────
    
    /**
     * @brief Get all gateway output since last clear.
     * @return Concatenated message bytes
     */
    const std::vector<uint8_t>& getGatewayOutput() const { return gatewayOutput_; }
    
    /**
     * @brief Get player-specific output (SELF_ENTITY messages).
     * @param pid Player to get output for
     * @return Message bytes for this player, or empty
     */
    std::vector<uint8_t> getPlayerOutput(PlayerId pid) const;
    
    /**
     * @brief Clear all captured outputs.
     */
    void clearOutputs();
    
    /**
     * @brief Check if any output was captured.
     * @return true if there's gateway output
     */
    bool hasOutput() const { return !gatewayOutput_.empty(); }
    
    // ── Assertions ───────────────────────────────────────────────────────────
    
    /**
     * @brief Assert player is at expected position (with tolerance).
     * @param pid Player to check
     * @param x,y,z Expected position in sub-voxels
     * @param tolerance Allowed difference (default 0)
     */
    void assertPosition(PlayerId pid, int32_t x, int32_t y, int32_t z, int32_t tolerance = 0);
    
    /**
     * @brief Assert player is within distance of expected position.
     * @param pid Player to check
     * @param x,y,z Expected position in sub-voxels
     * @param maxDistance Maximum allowed distance
     */
    void assertPositionNear(PlayerId pid, int32_t x, int32_t y, int32_t z, int32_t maxDistance);
    
    /**
     * @brief Assert player grounded state.
     * @param pid Player to check
     * @param grounded Expected grounded state
     */
    void assertGrounded(PlayerId pid, bool grounded);
    
    /**
     * @brief Assert player is in specific chunk.
     * @param pid Player to check
     * @param cx,cy,cz Expected chunk coordinates
     */
    void assertInChunk(PlayerId pid, int32_t cx, int32_t cy, int32_t cz);
    
    /**
     * @brief Assert player velocity components.
     * @param pid Player to check
     * @param vx,vy,vz Expected velocity in sub-voxels/tick
     * @param tolerance Allowed difference (default 0)
     */
    void assertVelocity(PlayerId pid, int32_t vx, int32_t vy, int32_t vz, int32_t tolerance = 0);
    
    // ── Raw Access ───────────────────────────────────────────────────────────
    
    /** @brief Access the ECS registry directly. */
    entt::registry& registry() { return engine_.getRegistry(); }
    
    /** @brief Access the game engine directly. */
    GameEngine& engine() { return engine_; }
    
    /** @brief Access the chunk registry directly. */
    ChunkRegistry& chunks() { return engine_.getChunkRegistry(); }
    
    /** @brief Get all player IDs currently in the game. */
    std::vector<PlayerId> getAllPlayers();
    
    /** @brief Count total entities in registry. */
    size_t entityCount() const { return const_cast<GameEngine&>(engine_).getRegistry().storage<entt::entity>().size(); }

private:
    GameEngine engine_;
    GatewayId gatewayId_ = 1;
    PlayerId nextPlayerId_ = 1;
    int tickCount_ = 0;
    
    // Output capture
    std::vector<uint8_t> gatewayOutput_;
    std::unordered_map<PlayerId, std::vector<uint8_t>> playerOutputs_;
    
    // Track which entity belongs to which player
    std::unordered_map<PlayerId, entt::entity> playerEntities_;
    
    // Helper to parse and store gateway output
    void onGatewayOutput(GatewayId gw, const uint8_t* data, size_t size);
    void onPlayerOutput(PlayerId pid, const uint8_t* data, size_t size);
};

/**
 * @brief Parse a message batch into individual messages.
 * @param data Raw batch bytes
 * @return Vector of (type, payload) pairs
 */
std::vector<std::pair<uint8_t, std::vector<uint8_t>>> parseBatch(
    const std::vector<uint8_t>& data);

/**
 * @brief Find messages of a specific type in a batch.
 * @param batch Parsed batch from parseBatch()
 * @param type Message type to find
 * @return Vector of payloads matching the type
 */
std::vector<std::vector<uint8_t>> findMessagesOfType(
    const std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& batch,
    ServerMessageType type);

// ── Protocol Fixture Loaders ─────────────────────────────────────────────────

/**
 * @brief Load a hex fixture file as raw bytes.
 * 
 * Hex files contain space-separated hex bytes, with # comments ignored.
 * Files are loaded from tests/protocol_fixtures/ relative to project root.
 * 
 * @param relativePath Path relative to protocol_fixtures/ (e.g., "client_to_server/input/input_forward.hex")
 * @return Vector of bytes parsed from the hex file
 * @throws std::runtime_error if file not found or parse error
 */
std::vector<uint8_t> loadHexFixture(const std::string& relativePath);

/**
 * @brief Get the protocol fixtures directory path.
 * @return Absolute path to tests/protocol_fixtures/
 */
std::filesystem::path getFixturesDirectory();

// ── Test Helpers ────────────────────────────────────────────────────────────

/**
 * @brief Simple entity chunk assignment for tests.
 *
 * Updates chunk membership for all entities based on their current position.
 * Clears and rebuilds chunk.entities and chunk.presentPlayers.
 * Does NOT handle watchingPlayers or gateway logic.
 *
 * @param chunkRegistry  Chunk registry for accessing chunks.
 * @param registry       The ECS registry.
 */
inline void updateEntityChunks(
    ChunkRegistry& chunkRegistry,
    entt::registry& registry)
{
    using namespace voxelmmo;
    
    // Clear all chunk entity sets
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->entities.clear();
        chunkPtr->presentPlayers.clear();
        chunkPtr->leftEntities.clear();
    }

    // Rebuild from all living entities
    auto view = registry.view<DynamicPositionComponent, ChunkMembershipComponent>();
    view.each([&](entt::entity ent, DynamicPositionComponent& dyn, ChunkMembershipComponent& membership) {
        const ChunkId newChunkId = ChunkId::fromSubVoxelPos(dyn.x, dyn.y, dyn.z);
        
        // Track chunk changes for delta serialization
        if (membership.currentChunkId != newChunkId) {
            if (Chunk* oldChunk = chunkRegistry.getChunkMutable(membership.currentChunkId)) {
                oldChunk->leftEntities.insert(ent);
            }
            // Mark entity with CREATE_ENTITY delta type so new chunk serializes full state
            if (auto* dirty = registry.try_get<DirtyComponent>(ent)) {
                dirty->markCreated();
            }
            membership.currentChunkId = newChunkId;
        }
        
        // Always reset moved flag
        dyn.moved = false;

        // Add to current chunk
        if (Chunk* chunk = chunkRegistry.getChunkMutable(newChunkId)) {
            chunk->entities.insert(ent);
            if (auto* playerComp = registry.try_get<PlayerComponent>(ent)) {
                chunk->presentPlayers.insert(playerComp->playerId);
            }
        }
    });
}

/**
 * @brief Physics test environment for isolated physics testing.
 * 
 * Creates a controlled world with flat ground for predictable tests.
 */
class PhysicsTestEnv {
public:
    /**
     * @brief Create physics test environment with flat ground at y=groundY.
     * @param groundY World Y voxel coordinate for ground surface (default 10)
     */
    explicit PhysicsTestEnv(int32_t groundY = 10);
    
    /**
     * @brief Spawn an entity at the given position.
     * @param x,y,z Position in sub-voxels
     * @param mode Physics mode
     * @return Entity handle
     */
    entt::entity spawnEntity(int32_t x, int32_t y, int32_t z, PhysicsMode mode);
    
    /**
     * @brief Run physics for N ticks.
     * @param n Number of ticks
     */
    void tick(int n = 1);
    
    /**
     * @brief Get position of an entity.
     * @param ent Entity handle
     * @return Pointer to position component
     */
    DynamicPositionComponent* getPosition(entt::entity ent);
    
    /**
     * @brief Set velocity of an entity.
     * @param ent Entity handle
     * @param vx,vy,vz New velocity
     */
    void setVelocity(entt::entity ent, int32_t vx, int32_t vy, int32_t vz);
    
    entt::registry registry;
    ChunkRegistry chunks;
    
private:
    GlobalEntityId nextEntityId_ = 1;
    int32_t groundY_;
};

} // namespace voxelmmo
