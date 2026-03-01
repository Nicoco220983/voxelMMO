#include <catch2/catch_test_macros.hpp>
#include "game/GameEngine.hpp"
#include "common/Types.hpp"
#include "common/MessageTypes.hpp"

#include <array>
#include <cstring>
#include <set>

using namespace voxelmmo;

// ── Helpers ────────────────────────────────────────────────────────────────

/** Build a 12-byte player-input payload (three little-endian floats). */
static std::array<uint8_t, 12> makeInput(float vx, float vy, float vz) {
    std::array<uint8_t, 12> buf{};
    std::memcpy(buf.data() + 0, &vx, sizeof(float));
    std::memcpy(buf.data() + 4, &vy, sizeof(float));
    std::memcpy(buf.data() + 8, &vz, sizeof(float));
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

        if (msgLen >= 9 &&
            data[off] == static_cast<uint8_t>(ChunkMessageType::SNAPSHOT_COMPRESSED))
        {
            int64_t cid = 0;
            std::memcpy(&cid, data + off + 1, sizeof(int64_t));
            out.insert(cid);
        }
        off += msgLen;
    }
}

/**
 * Convenience: register a gateway, add a player, and immediately send a
 * zero-velocity input to make the player grounded.  This prevents the first
 * tick's physics step from snapping the player to FLOOR_Y and changing the
 * expected chunk coordinate.
 */
static void setupGroundedPlayer(GameEngine& engine, GatewayId gwId, PlayerId pid,
                                 float x, float y, float z)
{
    engine.registerGateway(gwId);
    engine.addPlayer(gwId, pid, x, y, z);
    // Setting zero velocity via handlePlayerInput marks the entity as grounded,
    // so stepPhysics() in the first tick will leave the position unchanged.
    const auto zeroInput = makeInput(0.0f, 0.0f, 0.0f);
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

    // Spawn at (0, 8, 0) → chunk (cx=0, cy=0, cz=0).
    // Zero-velocity input makes the player grounded so physics does not snap
    // them to FLOOR_Y on the first tick.
    setupGroundedPlayer(engine, gwId, pid, 0.0f, 8.0f, 0.0f);
    engine.tick();

    // Every chunk in the activation window must have been snapshotted:
    //   cx ∈ [-R, R], cy ∈ {cy-1, cy, cy+1}, cz ∈ [-R, R]
    // where R = ACTIVATION_RADIUS = 2 and cy = floor(8/16) = 0.
    constexpr int R = GameEngine::ACTIVATION_RADIUS;
    for (int32_t dx = -R; dx <= R; ++dx) {
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dz = -R; dz <= R; ++dz) {
                const int64_t cid = ChunkId::make(static_cast<int8_t>(dy),
                                                   dx, dz).packed;
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
    setupGroundedPlayer(engine, gwId, pid, 0.0f, 8.0f, 0.0f);
    engine.tick();  // initial snapshots for cx ∈ [-2, 2]

    const std::set<int64_t> initialSnapshots = snapshots;
    REQUIRE(!initialSnapshots.empty());

    // ── Move the player 5 chunks forward in X ─────────────────────────────
    // Target chunk: cx=5 (world x = 5×64 = 320 units), outside ACTIVATION_RADIUS=2.
    // With handlePlayerInput the player is grounded, so stepPhysics advances
    // position by vx×TICK_DT without gravity.  We pick a velocity that crosses
    // exactly 5 chunk widths in one tick: vx = 320 / TICK_DT = 6 400 units/s.
    const float vx = 5.0f * static_cast<float>(CHUNK_SIZE_X) / TICK_DT;
    engine.handlePlayerInput(pid, makeInput(vx, 0.0f, 0.0f).data(), 12);

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
                const int64_t cid = ChunkId::make(static_cast<int8_t>(dy),
                                                   cx, dz).packed;
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

    setupGroundedPlayer(engine, gwId, pid, 0.0f, 8.0f, 0.0f);
    engine.tick();  // load all chunks around origin

    snapshots.clear();  // discard initial batch

    // Move 1 unit: player stays in cx=0 (chunk width = 64 units).
    // Activation window is unchanged → no new chunks → no snapshots.
    const float vx = 1.0f / TICK_DT;   // 1 unit in one tick
    engine.handlePlayerInput(pid, makeInput(vx, 0.0f, 0.0f).data(), 12);
    engine.tick();

    REQUIRE(snapshots.empty());
}
