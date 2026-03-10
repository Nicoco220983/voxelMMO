#include <catch2/catch_test_macros.hpp>
#include "TestUtils.hpp"
#include "game/systems/PhysicsSystem.hpp"
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"

using namespace voxelmmo;

// ── PhysicsTestEnv Tests ─────────────────────────────────────────────────────

TEST_CASE("PhysicsTestEnv creates flat ground", "[physics]") {
    PhysicsTestEnv env(10);  // Ground at y=10
    
    // Check ground exists
    auto* chunk = env.chunks.getChunk(ChunkId::make(0, 0, 0));
    REQUIRE(chunk != nullptr);
    
    // Ground should be stone
    CHECK(chunk->world.getVoxel(0, 10, 0) == VoxelTypes::STONE);
    // Above should be air
    CHECK(chunk->world.getVoxel(0, 11, 0) == VoxelTypes::AIR);
}

// ── GHOST Physics Tests ───────────────────────────────────────────────────────

TEST_CASE("GHOST entity moves freely through ground", "[physics]") {
    PhysicsTestEnv env(10);
    
    // Spawn ghost below ground
    auto ent = env.spawnEntity(128, 256 * 5, 128, PhysicsMode::GHOST);
    env.setVelocity(ent, 0, -1000, 0);  // Moving down fast
    
    env.tick(10);
    
    auto* pos = env.getPosition(ent);
    REQUIRE(pos->y < 256 * 10);  // Passed through ground
    REQUIRE(pos->grounded == true);  // Ghost always reports grounded
}

TEST_CASE("GHOST entity with zero velocity doesn't move", "[physics]") {
    PhysicsTestEnv env(10);
    
    auto ent = env.spawnEntity(128, 1000, 128, PhysicsMode::GHOST);
    env.setVelocity(ent, 0, 0, 0);
    
    env.tick(10);
    
    auto* pos = env.getPosition(ent);
    CHECK(pos->x == 128);
    CHECK(pos->y == 1000);
    CHECK(pos->z == 128);
}

TEST_CASE("GHOST entity moves at constant velocity", "[physics]") {
    PhysicsTestEnv env(10);
    
    auto ent = env.spawnEntity(0, 0, 0, PhysicsMode::GHOST);
    env.setVelocity(ent, 10, 20, 30);
    
    env.tick(5);
    
    auto* pos = env.getPosition(ent);
    CHECK(pos->x == 50);   // 10 * 5
    CHECK(pos->y == 100);  // 20 * 5
    CHECK(pos->z == 150);  // 30 * 5
    CHECK(pos->vx == 10);  // Unchanged
    CHECK(pos->vy == 20);
    CHECK(pos->vz == 30);
}

// ── FULL Physics Tests ────────────────────────────────────────────────────────

TEST_CASE("FULL entity falls due to gravity", "[physics]") {
    PhysicsTestEnv env(10);
    
    // Spawn well above ground
    int32_t startY = 256 * 15;  // y=15, ground is at y=10
    auto ent = env.spawnEntity(128, startY, 128, PhysicsMode::FULL);
    
    env.tick(1);
    
    auto* pos = env.getPosition(ent);
    CHECK(pos->vy < 0);     // Moving down (gravity applied)
    CHECK(pos->y < startY); // Fell
}

TEST_CASE("FULL entity lands on ground", "[physics]") {
    PhysicsTestEnv env(10);
    
    // Spawn above ground (ground is at y=2560, player bbox hy=~230)
    int32_t spawnY = 256 * 15;
    auto ent = env.spawnEntity(128, spawnY, 128, PhysicsMode::FULL);
    
    // Fall until grounded
    for (int i = 0; i < 200; ++i) {
        env.tick(1);
        if (env.getPosition(ent)->grounded) break;
    }
    
    auto* pos = env.getPosition(ent);
    CHECK(pos->grounded == true);
    CHECK(pos->vy == 0);  // Stopped falling
    CHECK(pos->y >= 256 * 10 + PLAYER_BBOX_HY);  // Resting on ground
}

TEST_CASE("FULL entity has terminal velocity", "[physics]") {
    PhysicsTestEnv env(10);
    
    auto ent = env.spawnEntity(0, 100000, 0, PhysicsMode::FULL);
    
    // Fall for many ticks
    env.tick(200);
    
    auto* pos = env.getPosition(ent);
    CHECK(pos->vy >= -TERMINAL_VELOCITY);
}

TEST_CASE("FULL entity stops at ground", "[physics]") {
    PhysicsTestEnv env(10);
    
    // Spawn on ground
    int32_t groundY = 256 * 10 + PLAYER_BBOX_HY;
    auto ent = env.spawnEntity(128, groundY + 500, 128, PhysicsMode::FULL);
    
    // Clear any velocity, let gravity do its work
    env.setVelocity(ent, 0, 0, 0);
    
    // Wait to land
    for (int i = 0; i < 100; ++i) {
        env.tick();
        if (env.getPosition(ent)->grounded) break;
    }
    
    auto* pos = env.getPosition(ent);
    CHECK(pos->grounded);
    CHECK(pos->vy == 0);  // Velocity zeroed on landing
}

TEST_CASE("FULL entity maintains horizontal velocity in air", "[physics]") {
    PhysicsTestEnv env(10);
    
    auto ent = env.spawnEntity(128, 5000, 128, PhysicsMode::FULL);
    env.setVelocity(ent, 50, 0, 0);
    
    env.tick(10);
    
    auto* pos = env.getPosition(ent);
    // Horizontal velocity preserved (no friction in air)
    CHECK(pos->vx == 50);
}

// ── FLYING Physics Tests ──────────────────────────────────────────────────────

TEST_CASE("FLYING entity has no gravity", "[physics]") {
    PhysicsTestEnv env(10);
    
    auto ent = env.spawnEntity(128, 5000, 128, PhysicsMode::FLYING);
    env.setVelocity(ent, 0, 0, 0);
    
    env.tick(50);
    
    auto* pos = env.getPosition(ent);
    CHECK(pos->y == 5000);  // Didn't fall
    CHECK(pos->grounded == false);  // Never grounded
}

TEST_CASE("FLYING entity collides with ground", "[physics]") {
    PhysicsTestEnv env(10);
    
    auto ent = env.spawnEntity(128, 256 * 12, 128, PhysicsMode::FLYING);
    env.setVelocity(ent, 0, -500, 0);  // Moving down
    
    env.tick(20);
    
    auto* pos = env.getPosition(ent);
    // Should have hit ground and stopped
    CHECK(pos->y >= 256 * 10 + PLAYER_BBOX_HY);
    CHECK(pos->grounded == false);  // Flying never grounded
}

TEST_CASE("FLYING entity preserves velocity on no collision", "[physics]") {
    PhysicsTestEnv env(10);
    
    auto ent = env.spawnEntity(128, 5000, 128, PhysicsMode::FLYING);
    env.setVelocity(ent, 30, 40, 50);
    
    env.tick(5);
    
    auto* pos = env.getPosition(ent);
    CHECK(pos->vx == 30);  // Unchanged
    CHECK(pos->vy == 40);
    CHECK(pos->vz == 50);
}

// ── Collision Tests ───────────────────────────────────────────────────────────

TEST_CASE("Entity doesn't fall through thin floor", "[physics]") {
    PhysicsTestEnv env(0);  // Ground at y=0
    
    auto ent = env.spawnEntity(128, 300, 128, PhysicsMode::FULL);
    
    env.tick(100);
    
    auto* pos = env.getPosition(ent);
    CHECK(pos->y >= PLAYER_BBOX_HY);  // Didn't fall through
    CHECK(pos->grounded);
}

TEST_CASE("Entity collides with ceiling", "[physics]") {
    PhysicsTestEnv env(10);
    
    auto ent = env.spawnEntity(128, 256 * 12, 128, PhysicsMode::FLYING);
    // Create ceiling above
    auto* chunk = env.chunks.getChunkMutable(ChunkId::make(0, 0, 0));
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            chunk->world.voxels[20 * CHUNK_SIZE_X * CHUNK_SIZE_Z + x * CHUNK_SIZE_Z + z] = VoxelTypes::STONE;
        }
    }
    
    env.setVelocity(ent, 0, 500, 0);  // Moving up fast
    int32_t startY = env.getPosition(ent)->y;
    
    env.tick(10);
    
    auto* pos = env.getPosition(ent);
    // Should have hit ceiling
    CHECK(pos->y < startY + 500 * 10);  // Didn't go full distance
}

// ── Integration with Chunk Membership ─────────────────────────────────────────

TEST_CASE("Entity moving between chunks updates membership", "[physics]") {
    PhysicsTestEnv env(10);
    
    // Spawn at edge of chunk
    int32_t edgeX = CHUNK_SIZE_X * SUBVOXEL_SIZE - 100;
    auto ent = env.spawnEntity(edgeX, 256 * 12, 128, PhysicsMode::GHOST);
    env.setVelocity(ent, 200, 0, 0);  // Moving toward next chunk
    
    ChunkId startChunk = ChunkId::fromSubVoxelPos(edgeX, 256 * 12, 128);
    
    env.tick(10);
    
    auto* pos = env.getPosition(ent);
    ChunkId endChunk = ChunkId::fromSubVoxelPos(pos->x, pos->y, pos->z);
    
    // Should have moved to next chunk
    CHECK(endChunk.x() == startChunk.x() + 1);
}
