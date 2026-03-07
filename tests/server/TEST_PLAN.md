# Server Test Suite Plan

## Overview

This document outlines the test suite for the voxelMMO server. The goal is to provide comprehensive coverage with minimal, focused tests that are easy to debug when they fail.

## Test Philosophy

1. **Unit tests** test one thing at a time with clear Arrange-Act-Assert structure
2. **Integration tests** use the `TestEnv` helper to set up full game scenarios
3. **Deterministic tests** use fixed seeds to avoid flaky tests
4. **Fast tests** avoid unnecessary sleeps or timeouts

## Test Utilities (tests/server/TestUtils.hpp)

A comprehensive test helper that wraps GameEngine and provides:

```cpp
class TestEnv {
public:
    // Construction with optional seed for determinism
    explicit TestEnv(uint32_t seed = 12345);
    
    // Player management helpers
    PlayerId addPlayer(EntityType type = EntityType::GHOST_PLAYER);
    void removePlayer(PlayerId pid);
    void teleport(PlayerId pid, int32_t x, int32_t y, int32_t z);
    
    // Input simulation
    void setInput(PlayerId pid, uint8_t buttons, float yaw = 0, float pitch = 0);
    void pressButton(PlayerId pid, InputButton btn);
    void releaseButton(PlayerId pid, InputButton btn);
    
    // Entity accessors
    entt::entity getEntity(PlayerId pid);
    DynamicPositionComponent* getPosition(PlayerId pid);
    
    // Chunk access
    Chunk* getChunk(int32_t cx, int32_t cy, int32_t cz);
    bool hasChunk(int32_t cx, int32_t cy, int32_t cz);
    
    // Game loop control
    void tick(int n = 1);  // Advance N ticks
    
    // Network output capture
    std::vector<uint8_t> getGatewayOutput(GatewayId gw = 1);
    std::vector<uint8_t> getPlayerOutput(PlayerId pid);
    void clearOutputs();
    
    // Assertions (throw on failure)
    void assertPosition(PlayerId pid, int32_t x, int32_t y, int32_t z, int32_t tolerance = 0);
    void assertGrounded(PlayerId pid, bool grounded);
    void assertInChunk(PlayerId pid, int32_t cx, int32_t cy, int32_t cz);
    
    // Direct registry access for custom checks
    entt::registry& registry() { return engine.getRegistry(); }
    GameEngine& engine() { return engine; }
    
private:
    GameEngine engine_;
    uint32_t nextPlayerId_ = 1;
    uint32_t nextGatewayId_ = 1;
    
    // Captured outputs
    std::unordered_map<GatewayId, std::vector<uint8_t>> gatewayOutputs_;
    std::unordered_map<PlayerId, std::vector<uint8_t>> playerOutputs_;
};
```

## Test File Organization

```
tests/server/
├── TestUtils.hpp/cpp         # Test environment helper
├── test_types.cpp            # ChunkId, VoxelIndex packing
├── test_network_protocol.cpp # Message serialization
├── test_components.cpp       # Individual component behavior
├── test_entity_factory.cpp   # Entity spawning
├── test_physics.cpp          # Physics system (isolated)
├── test_input_system.cpp     # Input velocity calculation
├── test_chunk_membership.cpp # Chunk tracking for entities
├── test_game_engine.cpp      # Integration tests using TestEnv
└── test_world_gen.cpp        # Existing terrain generation tests
```

## Detailed Test Plan

### 1. Types Tests (test_types.cpp)

Test the low-level type packing:

```cpp
TEST_CASE("ChunkId roundtrips coordinates") {
    // Test positive, negative, boundary values
}

TEST_CASE("VoxelIndex packing") {
    // packVoxelIndex / unpackVoxelIndex roundtrip
}

TEST_CASE("chunkIdOf computes correct chunk from position") {
    // Verify sub-voxel to chunk coordinate conversion
}
```

### 2. Network Protocol Tests (test_network_protocol.cpp)

```cpp
TEST_CASE("parseInput extracts buttons, yaw, pitch") {
    uint8_t data[] = {0, 13, 0, 0x0F,  /* yaw bytes */, /* pitch bytes */};
    auto msg = NetworkProtocol::parseInput(data, sizeof(data));
    REQUIRE(msg.has_value());
    REQUIRE(msg->buttons == 0x0F);
}

TEST_CASE("parseInput returns nullopt for short buffer") {
    uint8_t data[] = {0, 5, 0};  // Too short
    auto msg = NetworkProtocol::parseInput(data, sizeof(data));
    REQUIRE(!msg.has_value());
}

TEST_CASE("parseJoin extracts entity type") {
    uint8_t data[] = {1, 5, 0, static_cast<uint8_t>(EntityType::PLAYER)};
    auto msg = NetworkProtocol::parseJoin(data, sizeof(data));
    REQUIRE(msg.has_value());
    REQUIRE(msg->entityType == EntityType::PLAYER);
}

TEST_CASE("buildSelfEntityMessage format") {
    auto msg = NetworkProtocol::buildSelfEntityMessage(42, 100);
    REQUIRE(msg[0] == static_cast<uint8_t>(ServerMessageType::SELF_ENTITY));
    // Verify size, entity ID, tick fields
}
```

### 3. Component Tests (test_components.cpp)

```cpp
TEST_CASE("DynamicPositionComponent::modify updates position") {
    entt::registry reg;
    auto ent = reg.create();
    reg.emplace<DynamicPositionComponent>(ent, 0, 0, 0, 0, 0, 0, false);
    reg.emplace<DirtyComponent>(ent);
    
    DynamicPositionComponent::modify(reg, ent, 100, 200, 300, 10, 20, 30, true, true);
    
    auto& dyn = reg.get<DynamicPositionComponent>(ent);
    REQUIRE(dyn.x == 100);
    REQUIRE(dyn.grounded == true);
    REQUIRE(reg.get<DirtyComponent>(ent).isSnapshotDirty());
}

TEST_CASE("DirtyComponent lifecycle bits") {
    DirtyComponent dirty;
    REQUIRE(!dirty.isCreated());
    dirty.markCreated();
    REQUIRE(dirty.isCreated());
    dirty.clearSnapshot();
    REQUIRE(!dirty.isCreated());
}
```

### 4. Entity Factory Tests (test_entity_factory.cpp)

```cpp
TEST_CASE("EntityFactory queues and creates entities") {
    EntityFactory factory;
    factory.registerSpawnImpl(EntityType::SHEEP, 
        [](entt::registry& r, GlobalEntityId id, const EntitySpawnRequest& req) {
            // Simplified spawn for testing
            auto ent = r.create();
            r.emplace<GlobalEntityIdComponent>(ent, id);
            r.emplace<EntityTypeComponent>(ent, req.type);
            return ent;
        });
    
    factory.spawn(EntityType::SHEEP, 100, 200, 300);
    REQUIRE(factory.hasPendingSpawns());
    
    entt::registry reg;
    GlobalEntityId nextId = 1;
    auto created = factory.createEntities(reg, [&]() { return nextId++; });
    
    REQUIRE(created.size() == 1);
    REQUIRE(!factory.hasPendingSpawns());
    REQUIRE(reg.get<GlobalEntityIdComponent>(created[0]).id == 1);
}
```

### 5. Physics System Tests (test_physics.cpp)

Test physics in isolation with a controlled environment:

```cpp
// Helper: Create a flat world at y=10
class PhysicsTestEnv {
public:
    PhysicsTestEnv() {
        // Create chunks with solid ground at y=10
        // Register spawn implementations
    }
    
    entt::entity spawnEntity(int32_t x, int32_t y, int32_t z, PhysicsMode mode);
    void runTicks(int n);
    
    entt::registry registry;
    ChunkRegistry chunks;
};

TEST_CASE("GHOST entity passes through ground") {
    PhysicsTestEnv env;
    auto ent = env.spawnEntity(0, 500, 0, PhysicsMode::GHOST);  // High above
    auto& dyn = env.registry.get<DynamicPositionComponent>(ent);
    dyn.vy = -100;  // Moving down fast
    
    env.runTicks(10);
    
    // Should have moved through ground without collision
    auto& finalDyn = env.registry.get<DynamicPositionComponent>(ent);
    REQUIRE(finalDyn.y < 256 * 10);  // Below ground level
}

TEST_CASE("FULL entity collides with ground") {
    PhysicsTestEnv env;
    auto ent = env.spawnEntity(128, 300, 128, PhysicsMode::FULL);  // Above ground
    
    // Let gravity pull it down
    env.runTicks(50);
    
    auto& dyn = env.registry.get<DynamicPositionComponent>(ent);
    REQUIRE(dyn.grounded);  // Should land on ground
    REQUIRE(dyn.y >= 256 * 10 + PLAYER_BBOX_HY);  // Resting on surface
}

TEST_CASE("FULL entity terminal velocity") {
    PhysicsTestEnv env;
    auto ent = env.spawnEntity(0, 10000, 0, PhysicsMode::FULL);
    
    env.runTicks(100);
    
    auto& dyn = env.registry.get<DynamicPositionComponent>(ent);
    REQUIRE(dyn.vy >= -TERMINAL_VELOCITY);
}

TEST_CASE("FLYING entity collides but no gravity") {
    PhysicsTestEnv env;
    auto ent = env.spawnEntity(128, 300, 128, PhysicsMode::FLYING);
    
    env.runTicks(10);
    
    auto& dyn = env.registry.get<DynamicPositionComponent>(ent);
    REQUIRE(!dyn.grounded);  // Flying never grounded
    REQUIRE(dyn.vy == 0);     // No gravity
}
```

### 6. Input System Tests (test_input_system.cpp)

```cpp
TEST_CASE("GHOST_PLAYER 3D flight controls") {
    entt::registry reg;
    auto ent = reg.create();
    reg.emplace<InputComponent>(ent, 0, 0.0f, 0.0f);
    reg.emplace<DynamicPositionComponent>(ent, 0, 0, 0, 0, 0, 0, false);
    reg.emplace<EntityTypeComponent>(ent, EntityType::GHOST_PLAYER);
    reg.emplace<DirtyComponent>(ent);
    
    // Press forward only
    reg.get<InputComponent>(ent).buttons = static_cast<uint8_t>(InputButton::FORWARD);
    
    InputSystem::apply(reg);
    
    auto& dyn = reg.get<DynamicPositionComponent>(ent);
    REQUIRE(dyn.vz < 0);  // Moving forward (negative Z)
    REQUIRE(dyn.vx == 0); // No strafe
}

TEST_CASE("PLAYER jump only when grounded") {
    entt::registry reg;
    auto ent = reg.create();
    reg.emplace<InputComponent>(ent, 0, 0.0f, 0.0f);
    reg.emplace<DynamicPositionComponent>(ent, 0, 100, 0, 0, 0, 0, true);  // grounded
    reg.emplace<EntityTypeComponent>(ent, EntityType::PLAYER);
    reg.emplace<DirtyComponent>(ent);
    
    reg.get<InputComponent>(ent).buttons = static_cast<uint8_t>(InputButton::JUMP);
    
    InputSystem::apply(reg);
    
    auto& dyn = reg.get<DynamicPositionComponent>(ent);
    REQUIRE(dyn.vy == PLAYER_JUMP_VY);
}

TEST_CASE("PLAYER no jump when airborne") {
    entt::registry reg;
    auto ent = reg.create();
    reg.emplace<InputComponent>(ent, 0, 0.0f, 0.0f);
    reg.emplace<DynamicPositionComponent>(ent, 0, 100, 0, 0, 0, 0, false);  // not grounded
    reg.emplace<EntityTypeComponent>(ent, EntityType::PLAYER);
    reg.emplace<DirtyComponent>(ent);
    
    reg.get<InputComponent>(ent).buttons = static_cast<uint8_t>(InputButton::JUMP);
    
    int32_t prevVy = reg.get<DynamicPositionComponent>(ent).vy;
    InputSystem::apply(reg);
    
    auto& dyn = reg.get<DynamicPositionComponent>(ent);
    REQUIRE(dyn.vy == prevVy);  // Unchanged
}
```

### 7. Chunk Membership Tests (test_chunk_membership.cpp)

```cpp
TEST_CASE("Entity movement updates chunk membership") {
    entt::registry reg;
    ChunkRegistry chunks;
    
    // Create chunk (0,0,0)
    auto chunk0 = chunks.createOrGet(ChunkId::make(0, 0, 0));
    
    // Spawn entity in chunk (0,0,0)
    auto ent = reg.create();
    reg.emplace<DynamicPositionComponent>(ent, 100, 100, 100, 0, 0, 0, true);
    reg.emplace<GlobalEntityIdComponent>(ent, 1);
    chunk0->entities.insert(ent);
    
    // Move entity to chunk (1,0,0)
    auto& dyn = reg.get<DynamicPositionComponent>(ent);
    dyn.x = CHUNK_SIZE_X * SUBVOXEL_SIZE + 100;  // Into next chunk
    dyn.moved = true;
    
    // Create target chunk
    chunks.createOrGet(ChunkId::make(0, 1, 0));
    
    ChunkMembershipSystem::checkChunkMembership(reg, chunks);
    
    REQUIRE(chunk0->entities.empty());
    REQUIRE(chunk0->leftEntities.count(ent));
}
```

### 8. Game Engine Integration Tests (test_game_engine.cpp)

These use the full TestEnv helper:

```cpp
TEST_CASE("Player join creates entity and sends SELF_ENTITY", "[integration]") {
    TestEnv env(12345);  // Fixed seed
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    
    env.tick();
    
    // Verify entity exists
    auto ent = env.getEntity(pid);
    REQUIRE(ent != entt::null);
    
    // Verify SELF_ENTITY message was sent
    auto output = env.getPlayerOutput(pid);
    REQUIRE(output.size() > 0);
    REQUIRE(output[0] == static_cast<uint8_t>(ServerMessageType::SELF_ENTITY));
}

TEST_CASE("Player movement generates chunk activation", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    env.clearOutputs();
    
    // Teleport far away
    env.teleport(pid, 10000, 2000, 10000);
    env.tick();
    
    // Should have activated new chunks
    auto output = env.getGatewayOutput();
    REQUIRE(output.size() > 0);  // Snapshot messages
}

TEST_CASE("Ghost player flies with no gravity", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.teleport(pid, 0, 5000, 0);  // High up
    env.tick();
    
    // Press forward
    env.pressButton(pid, InputButton::FORWARD);
    env.tick();
    
    // Should move, no falling
    auto pos1 = env.getPosition(pid);
    REQUIRE(pos1->y == 5000);  // No gravity
    
    env.tick(10);
    
    auto pos2 = env.getPosition(pid);
    REQUIRE(pos2->y == 5000);  // Still no gravity
    REQUIRE(pos2->z < pos1->z);  // Moved forward
}

TEST_CASE("Player falls due to gravity and lands", "[integration]") {
    TestEnv env(12345);
    
    // Spawn high above ground
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.teleport(pid, 0, 5000, 0);
    env.tick();
    
    REQUIRE(!env.getPosition(pid)->grounded);
    
    // Let fall
    env.tick(100);
    
    env.assertGrounded(pid, true);
    auto pos = env.getPosition(pid);
    REQUIRE(pos->vy == 0);  // Stopped falling
}

TEST_CASE("Player walks on ground", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick();  // Spawn and land
    
    auto startPos = *env.getPosition(pid);
    REQUIRE(startPos.grounded);
    
    // Walk forward
    env.pressButton(pid, InputButton::FORWARD);
    env.tick(20);
    
    auto endPos = env.getPosition(pid);
    REQUIRE(endPos->z < startPos.z);  // Moved forward
    REQUIRE(endPos->grounded);         // Still grounded
}

TEST_CASE("Player jumps and lands", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick();  // Land first
    env.assertGrounded(pid, true);
    
    auto startY = env.getPosition(pid)->y;
    
    // Jump
    env.pressButton(pid, InputButton::JUMP);
    env.tick();
    env.releaseButton(pid, InputButton::JUMP);
    
    REQUIRE(env.getPosition(pid)->vy > 0);  // Moving up
    
    // Wait for jump arc
    env.tick(50);
    
    env.assertGrounded(pid, true);
    REQUIRE(env.getPosition(pid)->y == startY);  // Back to ground
}

TEST_CASE("Entity cleanup on player disconnect", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::PLAYER);
    env.tick();
    
    auto ent = env.getEntity(pid);
    REQUIRE(ent != entt::null);
    
    env.removePlayer(pid);
    env.tick();
    
    // Entity should be destroyed
    REQUIRE(!env.registry().valid(ent));
}

TEST_CASE("Chunk snapshots sent to watching players", "[integration]") {
    TestEnv env(12345);
    
    PlayerId pid = env.addPlayer(EntityType::GHOST_PLAYER);
    env.tick();
    env.clearOutputs();
    
    // Move to trigger chunk updates
    env.teleport(pid, CHUNK_SIZE_X * SUBVOXEL_SIZE, 2000, 0);
    env.tick();
    
    auto output = env.getGatewayOutput();
    // Should contain CHUNK_SNAPSHOT or CHUNK_DELTA messages
    bool foundChunkMsg = false;
    for (size_t i = 0; i < output.size(); ) {
        uint8_t type = output[i];
        if (type <= 5) {  // CHUNK_SNAPSHOT through CHUNK_TICK_DELTA_COMPRESSED
            foundChunkMsg = true;
            break;
        }
        uint16_t size = *reinterpret_cast<const uint16_t*>(&output[i+1]);
        i += 3 + size;
    }
    REQUIRE(foundChunkMsg);
}

TEST_CASE("Multiple players in same chunk see each other", "[integration]") {
    TestEnv env(12345);
    
    PlayerId p1 = env.addPlayer(EntityType::GHOST_PLAYER);
    PlayerId p2 = env.addPlayer(EntityType::GHOST_PLAYER);
    
    // Put both in same location
    env.teleport(p1, 100, 2000, 100);
    env.teleport(p2, 200, 2000, 200);
    env.tick();
    
    // Both should receive snapshots containing the other player
    // (Would need to parse messages to fully verify)
}
```

### 9. World Generation Tests (test_world_gen.cpp) - EXISTING

Keep existing tests, possibly add:

```cpp
TEST_CASE("WorldGenerator surfaceY is within bounds") {
    WorldGenerator gen(12345);
    for (int x = -10; x <= 10; ++x) {
        for (int z = -10; z <= 10; ++z) {
            int32_t y = gen.surfaceY(x * 10.0f, z * 10.0f);
            REQUIRE(y >= 4);
            REQUIRE(y <= 30);
        }
    }
}

TEST_CASE("WorldGenerator produces valid spawn position") {
    WorldGenerator gen(12345);
    gen.computePlayerSpawnPos();
    auto pos = gen.getPlayerSpawnPos();
    REQUIRE(pos[1] > 0);  // Y > 0 (above bedrock)
}
```

## Running Tests

```bash
# Build tests
bash scripts/build.sh

# Run all tests
bash scripts/test.sh

# Run specific test file
./build/voxelmmo_tests "[physics]"

# Run specific test
./build/voxelmmo_tests "Player falls due to gravity and lands"

# Run with verbose output
./build/voxelmmo_tests -s "[integration]"
```

## Adding New Tests

1. Add test file to `tests/server/test_*.cpp`
2. Add file to `CMakeLists.txt` under `voxelmmo_tests` sources
3. Use descriptive test names in `TEST_CASE("description", "[tag]")` format
4. Use `TestEnv` for integration tests, create minimal setups for unit tests
5. Always clean up - use `TestEnv` which handles this automatically

## Debugging Failed Tests

When a test fails, the error message should tell you:
- What was expected
- What actually happened
- The line number in the test

Use the `-s` flag for detailed output:
```bash
./build/voxelmmo_tests -s "failed test name"
```
