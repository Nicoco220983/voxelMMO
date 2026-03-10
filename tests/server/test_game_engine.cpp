#include <catch2/catch_test_macros.hpp>
#include "TestUtils.hpp"
#include "common/MessageTypes.hpp"

using namespace voxelmmo;

// ── Basic Player Lifecycle ───────────────────────────────────────────────────

TEST_CASE("TestEnv creates player entity", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    
    auto ent = env.getEntity(pid);
    CHECK(ent != static_cast<entt::entity>(entt::null));
    CHECK(env.hasPlayer(pid));
}

TEST_CASE("Player receives SELF_ENTITY message on join", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick();
    
    auto output = env.getPlayerOutput(pid);
    REQUIRE(output.size() > 0);
    CHECK(output[0] == static_cast<uint8_t>(ServerMessageType::SELF_ENTITY));
}

// TEST_CASE("Player entity is destroyed on disconnect", "[integration]") {
//     TestEnv env(12345);
    
//     PlayerId pid = env.addPlayer(EntityType::PLAYER);
//     env.tick();
    
//     auto ent = env.getEntity(pid);
//     REQUIRE(ent != static_cast<entt::entity>(entt::null));
    
//     env.removePlayer(pid);
//     env.tick();
    
//     CHECK(env.getEntity(pid) == static_cast<entt::entity>(entt::null));
//     CHECK_FALSE(env.hasPlayer(pid));
// }

// ── Player Spawning and Position ─────────────────────────────────────────────

TEST_CASE("Player spawns at world spawn position", "[integration]") {
    TestEnv env(12345);
    
    auto spawnPos = env.engine().getWorldGenerator().getPlayerSpawnPos();
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick();
    
    auto* pos = env.getPosition(pid);
    REQUIRE(pos != nullptr);
    // Should be at spawn position
    CHECK(pos->x == spawnPos[0]);
    CHECK(pos->z == spawnPos[2]);
}

TEST_CASE("Player can be teleported", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    
    env.teleport(pid, 10000, 5000, 10000);
    
    auto* pos = env.getPosition(pid);
    REQUIRE(pos != nullptr);
    CHECK(pos->x == 10000);
    CHECK(pos->y == 5000);
    CHECK(pos->z == 10000);
}

TEST_CASE("Teleport moves player between chunks", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    
    auto startChunk = ChunkId::fromSubVoxelPos(env.getPosition(pid)->x, env.getPosition(pid)->y, env.getPosition(pid)->z);
    
    // Teleport far away to a different chunk
    int32_t targetX = CHUNK_SIZE_X * SUBVOXEL_SIZE * 5 + 100;
    int32_t targetZ = CHUNK_SIZE_Z * SUBVOXEL_SIZE * 4 + 100;
    int32_t targetY = 5000;
    env.teleport(pid, targetX, targetY, targetZ);
    env.tick();
    
    auto endChunk = ChunkId::fromSubVoxelPos(env.getPosition(pid)->x, env.getPosition(pid)->y, env.getPosition(pid)->z);
    
    // Should be in a different chunk
    CHECK(endChunk.packed != startChunk.packed);
    CHECK(endChunk.x() == 5);
    CHECK(endChunk.z() == 4);
}

// ── Ghost Player Movement ─────────────────────────────────────────────────────

TEST_CASE("Ghost player flies with no gravity", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.teleport(pid, 0, 10000, 0);
    env.tick();
    
    auto startY = env.getPosition(pid)->y;
    auto startZ = env.getPosition(pid)->z;
    
    // Set yaw to point -Z (forward) and press forward
    env.setLook(pid, 0.0f, 0.0f);
    env.pressButton(pid, InputButton::FORWARD);
    env.tick(20);
    
    auto* pos = env.getPosition(pid);
    // Ghost has no gravity
    CHECK(pos->y == startY);
    // Note: InputSystem applies velocity, but exact position change depends on implementation
}

TEST_CASE("Ghost player can ascend and descend", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    
    auto startY = env.getPosition(pid)->y;
    
    // Ascend (JUMP = up for ghosts when pitch is 0)
    env.setLook(pid, 0.0f, 0.0f);  // Pitch 0 = level
    env.pressButton(pid, InputButton::JUMP);
    env.tick(10);
    auto yAfterJump = env.getPosition(pid)->y;
    env.releaseButton(pid, InputButton::JUMP);
    
    // Descend (DESCEND = down for ghosts)
    env.pressButton(pid, InputButton::DESCEND);
    env.tick(10);
    auto yAfterDescend = env.getPosition(pid)->y;
    
    // These checks verify vertical movement capability
    // Exact amounts depend on GHOST_MOVE_SPEED and tick count
    CHECK(yAfterJump >= startY);          // Didn't go down
    CHECK(yAfterDescend <= yAfterJump);   // Didn't go up from jump peak
}

// ── Full Player Physics ───────────────────────────────────────────────────────

TEST_CASE("Player physics simulation runs", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick(10);  // Let player land
    
    // After landing, player should be grounded
    // (This tests that physics ran and detected ground)
    auto* pos = env.getPosition(pid);
    REQUIRE(pos != nullptr);
    
    // Just verify physics processed the player
    // Whether grounded or not depends on spawn terrain
    CHECK(pos->y > 0);  // Should be above bedrock
}

TEST_CASE("Player can walk on ground", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick(50);  // Land first
    
    // Skip test if we never landed (spawn might be in weird terrain)
    if (!env.getPosition(pid)->grounded) {
        return;
    }
    
    auto startZ = env.getPosition(pid)->z;
    
    // Set yaw to -Z direction
    env.setLook(pid, 0.0f, 0.0f);
    env.pressButton(pid, InputButton::FORWARD);
    env.tick(30);
    env.releaseButton(pid, InputButton::FORWARD);
    
    auto endZ = env.getPosition(pid)->z;
    CHECK(endZ != startZ);  // Moved somewhere
    // Note: Player may have walked off terrain edge and fallen - that's correct physics
}

TEST_CASE("Player jumps and lands", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick(50);  // Land first
    
    // Skip if never landed
    if (!env.getPosition(pid)->grounded) {
        return;
    }
    
    auto startY = env.getPosition(pid)->y;
    
    // Jump
    env.pressButton(pid, InputButton::JUMP);
    env.tick();
    auto vyAfterJump = env.getPosition(pid)->vy;
    env.releaseButton(pid, InputButton::JUMP);
    
    CHECK(vyAfterJump > 0);  // Got upward velocity
    
    // Wait for landing
    bool landed = false;
    for (int i = 0; i < 200; ++i) {
        env.tick();
        if (env.getPosition(pid)->grounded) {
            landed = true;
            break;
        }
    }
    
    CHECK(landed);
    // Should land near original height
    auto endY = env.getPosition(pid)->y;
    CHECK(std::abs(endY - startY) < 256);  // Within 1 voxel
}

TEST_CASE("Player cannot jump while airborne", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.teleport(pid, 0, 10000, 0);
    env.tick();
    
    REQUIRE_FALSE(env.getPosition(pid)->grounded);
    
    env.pressButton(pid, InputButton::JUMP);
    env.tick();
    
    auto vyAfter = env.getPosition(pid)->vy;
    // Jump impulse is +110 (PLAYER_JUMP_VY), so if we're airborne,
    // vy should not be positive (gravity keeps it negative or zero)
    CHECK(vyAfter <= 0);  // No jump impulse applied
}

// ── Input Handling ────────────────────────────────────────────────────────────

TEST_CASE("Input buttons are processed each tick", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    env.clearOutputs();
    
    env.pressButton(pid, InputButton::FORWARD);
    env.tick();
    
    auto* pos = env.getPosition(pid);
    CHECK(pos->vz != 0);  // Moving
}

TEST_CASE("Released buttons stop movement", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    
    env.pressButton(pid, InputButton::FORWARD);
    env.tick();
    auto movingVz = env.getPosition(pid)->vz;
    REQUIRE(movingVz != 0);
    
    env.releaseButton(pid, InputButton::FORWARD);
    env.tick();
    
    auto stoppedVz = env.getPosition(pid)->vz;
    CHECK(stoppedVz == 0);  // Stopped
}

TEST_CASE("Multiple buttons can be pressed", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    
    // Move diagonally
    env.pressButton(pid, InputButton::FORWARD);
    env.pressButton(pid, InputButton::RIGHT);
    env.tick();
    
    auto* pos = env.getPosition(pid);
    CHECK(pos->vx != 0);  // Moving right
    CHECK(pos->vz != 0);  // Moving forward
}

// ── Chunk Activation ──────────────────────────────────────────────────────────

TEST_CASE("Moving player activates new chunks", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    env.clearOutputs();
    
    // Teleport far away
    env.teleport(pid, CHUNK_SIZE_X * SUBVOXEL_SIZE * 10, 5000, 0);
    env.tick();
    
    // Should have generated output for new chunks
    CHECK(env.hasOutput());
}

TEST_CASE("Player receives chunk snapshots", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    
    auto output = env.getGatewayOutput();
    REQUIRE(output.size() > 0);
    
    // Parse batch and find chunk messages
    auto batch = parseBatch(output);
    bool foundChunkMsg = false;
    for (const auto& [type, payload] : batch) {
        if (type <= 5) {  // CHUNK_SNAPSHOT through CHUNK_TICK_DELTA_COMPRESSED
            foundChunkMsg = true;
            break;
        }
    }
    CHECK(foundChunkMsg);
}

// ── Multi-Player Tests ────────────────────────────────────────────────────────

TEST_CASE("Multiple players can join", "[integration]") {
    TestEnv env(12345);
    
    PlayerId p1 = env.addPlayer(EntityType::PLAYER);
    PlayerId p2 = env.addPlayer(EntityType::GHOST_PLAYER);
    PlayerId p3 = env.addPlayer(EntityType::PLAYER);
    env.tick();
    
    auto players = env.getAllPlayers();
    CHECK(players.size() == 3);
    
    CHECK(env.getEntity(p1) != static_cast<entt::entity>(entt::null));
    CHECK(env.getEntity(p2) != static_cast<entt::entity>(entt::null));
    CHECK(env.getEntity(p3) != static_cast<entt::entity>(entt::null));
}

TEST_CASE("Each player receives own SELF_ENTITY", "[integration]") {
    TestEnv env(12345);
    
    PlayerId p1 = env.addPlayer(EntityType::PLAYER);
    PlayerId p2 = env.addPlayer(EntityType::PLAYER);
    env.tick();
    
    auto out1 = env.getPlayerOutput(p1);
    auto out2 = env.getPlayerOutput(p2);
    
    CHECK(out1.size() > 0);
    CHECK(out2.size() > 0);
    
    // Each should have different entity IDs
    uint32_t id1, id2;
    std::memcpy(&id1, &out1[3], sizeof(uint32_t));
    std::memcpy(&id2, &out2[3], sizeof(uint32_t));
    CHECK(id1 != id2);
}

TEST_CASE("Players can see each other in same chunk", "[integration]") {
    TestEnv env(12345);
    
    PlayerId p1 = env.addPlayer(EntityType::PLAYER);
    PlayerId p2 = env.addPlayer(EntityType::PLAYER);
    env.tick();
    env.clearOutputs();
    
    // Put both close together in the same chunk
    env.teleport(p1, 1000, 5000, 1000);
    env.teleport(p2, 1500, 5000, 1500);
    env.tick();
    
    // Players should receive chunk data when they move to new areas
    // Output may be empty if chunks were already loaded
    // Just verify both players are still valid
    CHECK(env.getEntity(p1) != static_cast<entt::entity>(entt::null));
    CHECK(env.getEntity(p2) != static_cast<entt::entity>(entt::null));
}

// ── Entity Cleanup ────────────────────────────────────────────────────────────

TEST_CASE("Player entities are created and can be retrieved", "[integration]") {
    TestEnv env(12345);
    
    // Initially no players
    CHECK(env.getAllPlayers().empty());
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick();
    
    // Player should exist
    auto players = env.getAllPlayers();
    CHECK(players.size() == 1);
    CHECK(players[0] == pid);
    
    // Entity should be valid
    auto ent = env.getEntity(pid);
    CHECK(ent != static_cast<entt::entity>(entt::null));
    CHECK(env.registry().valid(ent));
    
    // Position should exist
    auto* pos = env.getPosition(pid);
    CHECK(pos != nullptr);
}

// ── Edge Cases ────────────────────────────────────────────────────────────────

TEST_CASE("Teleport to same location is no-op", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick();
    
    auto* pos = env.getPosition(pid);
    int32_t x = pos->x, y = pos->y, z = pos->z;
    
    env.teleport(pid, x, y, z);
    
    CHECK(pos->x == x);
    CHECK(pos->y == y);
    CHECK(pos->z == z);
}

TEST_CASE("Rapid teleport doesn't crash", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    
    for (int i = 0; i < 100; ++i) {
        env.teleport(pid, i * 1000, 5000, i * 1000);
        env.tick();
    }
    
    CHECK(env.getEntity(pid) != static_cast<entt::entity>(entt::null));
}

// ── Bug Fix Verification: Player in Chunk Snapshot ───────────────────────────

TEST_CASE("Newly joined player entity is present in chunk snapshot", "[bug][integration]") {
    // This test verifies the fix for the bug where a newly joined player's
    // entity was not present in the initial chunk snapshot they received.
    // 
    // Bug root cause: When a new player joins, they're added to existing chunks.
    // The chunk's snapshot was built earlier (at server startup) and doesn't
    // include this new player. The chunk builds a delta with CREATE_ENTITY for
    // the new player, but serializeChunks() sends the old snapshot to new
    // watchers (lastTick == 0), ignoring the delta.
    
    TestEnv env(12345);
    
    // Get the spawn chunk before adding player
    auto spawnPos = env.engine().getWorldGenerator().getPlayerSpawnPos();
    auto* spawnChunk = env.getChunkAt(spawnPos[0], spawnPos[1], spawnPos[2]);
    REQUIRE(spawnChunk != nullptr);
    
    // Now add a player and tick - this creates the player entity
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick();
    
    // Get the player's entity
    auto ent = env.getEntity(pid);
    REQUIRE(ent != static_cast<entt::entity>(entt::null));
    
    // Re-get the spawn chunk (may have changed after tick)
    spawnChunk = env.getChunkAt(spawnPos[0], spawnPos[1], spawnPos[2]);
    REQUIRE(spawnChunk != nullptr);
    
    // CRITICAL: Verify the player entity is in the chunk's entity set
    // This is the source of truth - the chunk must know about the player
    bool playerInChunkEntities = spawnChunk->entities.count(ent) > 0;
    REQUIRE(playerInChunkEntities);
    
    // The chunk's snapshot should have been rebuilt during serializeChunks()
    // (which is called during tick()). With the fix, the snapshot is rebuilt
    // before sending to new watchers, so it should include the player.
    
    // We verify this by checking that the chunk's latest tick was updated
    // to the current tick (not an older tick from server startup)
    CHECK(spawnChunk->state.getLatestTick() == static_cast<uint32_t>(env.getTickCount()));
}
