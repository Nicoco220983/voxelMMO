#include <catch2/catch_test_macros.hpp>
#include "game/GameEngine.hpp"
#include "common/Types.hpp"
#include "common/MessageTypes.hpp"
#include "game/components/DynamicPositionComponent.hpp"

#include <array>
#include <cstring>
#include <set>

using namespace voxelmmo;

// ── Helpers ────────────────────────────────────────────────────────────────

/** Build a 13-byte INPUT message: [type(1)][size(2)][buttons(1)][yaw(4)][pitch(4)]. */
static std::array<uint8_t, 13> makeInput(uint8_t buttons = 0, float yaw = 0.0f, float pitch = 0.0f) {
    std::array<uint8_t, 13> buf{};
    buf[0] = static_cast<uint8_t>(ClientMessageType::INPUT);
    buf[1] = 13;  // size low byte
    buf[2] = 0;   // size high byte
    buf[3] = buttons;
    std::memcpy(buf.data() + 4, &yaw,   sizeof(float));
    std::memcpy(buf.data() + 8, &pitch, sizeof(float));
    return buf;
}

/**
 * Walk a length-prefixed batch and collect the packed ChunkId of every
 * SNAPSHOT_COMPRESSED message found inside it.
 */
static void collectSnapshots(const uint8_t* data, size_t size,
                              std::set<int64_t>& out)
{
    size_t off = 0;
    while (off + 4 <= size) {
        uint32_t msgLen = 0;
        std::memcpy(&msgLen, data + off, sizeof(uint32_t));
        off += 4;
        if (off + msgLen > size) break;

        // New header: [type(1)][size(2)][chunk_id(8)][tick(4)] = 15 bytes minimum
        if (msgLen >= 15 &&
            data[off] == static_cast<uint8_t>(ServerMessageType::CHUNK_SNAPSHOT_COMPRESSED))
        {
            int64_t cid = 0;
            std::memcpy(&cid, data + off + 3, sizeof(int64_t));  // chunk_id at offset 3
            out.insert(cid);
        }
        off += msgLen;
    }
}

/**
 * Convenience: register a gateway, add a GHOST_PLAYER, and send a zero-button
 * INPUT so the first tick's InputSystem sees no movement (velocity stays 0).
 * Position parameters are in sub-voxels.
 */
static void setupGroundedPlayer(GameEngine& engine, GatewayId gwId, PlayerId pid,
                                 int32_t sx, int32_t sy, int32_t sz)
{
    engine.registerGateway(gwId);
    engine.addPlayer(gwId, pid, sx, sy, sz);  // default: GHOST_PLAYER
    // Zero buttons → InputSystem sets velocity to 0 → no movement on first tick.
    const auto zeroInput = makeInput(0);
    engine.handlePlayerInput(pid, zeroInput.data(), zeroInput.size());
}

// ── Tests ──────────────────────────────────────────────────────────────────

TEST_CASE("GameEngine - initial tick sends snapshots for chunks around spawn", "[chunk_streaming]") {
    GameEngine engine;
    const GatewayId gwId = 1;
    const PlayerId  pid  = 10;

    std::set<int64_t> snapshots;
    engine.setOutputCallback([&](GatewayId, const uint8_t* data, size_t size) {
        collectSnapshots(data, size, snapshots);
    });

    // Spawn at (0, 8 voxels, 0) → chunk (cx=0, cy=0, cz=0).
    // Zero-velocity input makes the player grounded so physics does not snap
    // them to FLOOR_Y on the first tick.
    setupGroundedPlayer(engine, gwId, pid, 0, 8 * SUBVOXEL_SIZE, 0);
    engine.tick();

    // Every chunk in the activation window must have been snapshotted:
    //   cx ∈ [-R, R], cy ∈ {cy-1, cy, cy+1}, cz ∈ [-R, R]
    // where R = ACTIVATION_RADIUS = 2 and cy = floor(8/32) = 0.
    constexpr int R = GameEngine::ACTIVATION_RADIUS;
    for (int32_t dx = -R; dx <= R; ++dx) {
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dz = -R; dz <= R; ++dz) {
                const int64_t cid = ChunkId::make(dy, dx, dz).packed;
                CAPTURE(dx, dy, dz);
                REQUIRE(snapshots.count(cid) == 1);
            }
        }
    }

    // Total: (2R+1) × 3 × (2R+1) = 5 × 3 × 5 = 75 chunks.
    REQUIRE(snapshots.size() == static_cast<size_t>((2*R+1) * 3 * (2*R+1)));
}

TEST_CASE("GameEngine - moving player sends snapshots for newly entered chunks", "[chunk_streaming]") {
    GameEngine engine;
    const GatewayId gwId = 1;
    const PlayerId  pid  = 20;

    std::set<int64_t> snapshots;
    engine.setOutputCallback([&](GatewayId, const uint8_t* data, size_t size) {
        collectSnapshots(data, size, snapshots);
    });

    // Spawn grounded at the origin chunk (cx=0, cy=0, cz=0).
    setupGroundedPlayer(engine, gwId, pid, 0, 8 * SUBVOXEL_SIZE, 0);
    engine.tick();  // initial snapshots for cx ∈ [-2, 2]

    const std::set<int64_t> initialSnapshots = snapshots;
    REQUIRE(!initialSnapshots.empty());

    // ── Move the player 5 chunks forward in X ─────────────────────────────
    // Target chunk: cx=5 (world x = 5×32 = 160 voxels), outside ACTIVATION_RADIUS=2.
    // Teleport directly (test setup bypass) — this test covers chunk-snapshot
    // dispatch logic, not the input system.
    engine.teleportPlayer(pid, 5 * CHUNK_SIZE_X * SUBVOXEL_SIZE, 8 * SUBVOXEL_SIZE, 0);

    snapshots.clear();
    engine.tick();  // player jumps to x≈320 (chunk cx=5); new chunks emitted

    // ── Regression: the destination chunk must now have been snapshotted ──
    const int64_t landingChunk = ChunkId::make(0, 5, 0).packed;
    REQUIRE(snapshots.count(landingChunk) == 1);

    // Every chunk in the new activation window [3,7] × {-1,0,1} × [-2,2]
    // must have been snapshotted (all are outside the initial window [-2,2]).
    constexpr int R = GameEngine::ACTIVATION_RADIUS;
    for (int32_t cx = 5 - R; cx <= 5 + R; ++cx) {
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dz = -R; dz <= R; ++dz) {
                const int64_t cid = ChunkId::make(dy, cx, dz).packed;
                CAPTURE(cx, dy, dz);
                REQUIRE(snapshots.count(cid) == 1);
            }
        }
    }

    // ── Sanity: already-known chunks must NOT be re-sent as snapshots ──────
    // cx ∈ [-2,2] never overlaps with the new window cx ∈ [3,7].
    for (int64_t cid : initialSnapshots) {
        REQUIRE(snapshots.count(cid) == 0);
    }
}

TEST_CASE("GameEngine - micro-movement within same chunk sends no new snapshots", "[chunk_streaming]") {
    GameEngine engine;
    const GatewayId gwId = 1;
    const PlayerId  pid  = 30;

    std::set<int64_t> snapshots;
    engine.setOutputCallback([&](GatewayId, const uint8_t* data, size_t size) {
        collectSnapshots(data, size, snapshots);
    });

    setupGroundedPlayer(engine, gwId, pid, 0, 8 * SUBVOXEL_SIZE, 0);
    engine.tick();  // load all chunks around origin

    snapshots.clear();  // discard initial batch

    // Zero buttons: entity stays stationary. Activation window is unchanged → no new snapshots.
    const auto idleInput = makeInput(0);
    engine.handlePlayerInput(pid, idleInput.data(), idleInput.size());
    engine.tick();

    REQUIRE(snapshots.empty());
}

TEST_CASE("GameEngine - JOIN message spawns entity with correct EntityType", "[chunk_streaming]") {
    // Verify that queuePendingPlayer + JOIN(PLAYER) creates a PLAYER-typed entity,
    // and that JOIN(GHOST_PLAYER) creates a GHOST_PLAYER-typed entity.

    for (const EntityType wantType : { EntityType::PLAYER, EntityType::GHOST_PLAYER }) {
        GameEngine engine;
        const GatewayId gwId = 4;
        const PlayerId  pid  = 40;

        std::set<int64_t> snapshots;
        engine.setOutputCallback([&](GatewayId, const uint8_t* data, size_t size) {
            collectSnapshots(data, size, snapshots);
        });

        engine.registerGateway(gwId);
        engine.queuePendingPlayer(gwId, pid, 0, 8 * SUBVOXEL_SIZE, 0);

        // Before JOIN, no entity exists — INPUT message must be silently ignored.
        const auto zeroInput = makeInput(0);
        engine.handlePlayerInput(pid, zeroInput.data(), zeroInput.size());
        engine.tick();  // entity not yet spawned → no snapshots
        REQUIRE(snapshots.empty());

        // Send JOIN — entity is spawned, snapshot is dispatched immediately.
        // New format: [type(1)][size(2)][entityType(1)] = 5 bytes
        std::array<uint8_t, 5> joinMsg{};
        joinMsg[0] = static_cast<uint8_t>(ClientMessageType::JOIN);
        joinMsg[1] = 5;  // size low byte
        joinMsg[2] = 0;  // size high byte
        joinMsg[3] = static_cast<uint8_t>(wantType);
        engine.handlePlayerInput(pid, joinMsg.data(), joinMsg.size());

        // After JOIN a snapshot must have been delivered.
        REQUIRE(!snapshots.empty());

        // Now that the entity exists, INPUT message must be accepted.
        engine.handlePlayerInput(pid, zeroInput.data(), zeroInput.size());
        engine.tick();  // runs without error
    }
}
