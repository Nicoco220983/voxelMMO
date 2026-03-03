#include <catch2/catch_test_macros.hpp>
#include "game/GameEngine.hpp"
#include "common/Types.hpp"
#include "common/MessageTypes.hpp"
#include "game/components/DynamicPositionComponent.hpp"

#include <array>
#include <cstring>
#include <vector>

using namespace voxelmmo;

// ── Helpers ────────────────────────────────────────────────────────────────

/** Build a 10-byte INPUT message. */
static std::array<uint8_t, 10> makeInput(uint8_t buttons = 0,
                                          float yaw = 0.0f,
                                          float pitch = 0.0f)
{
    std::array<uint8_t, 10> buf{};
    buf[0] = static_cast<uint8_t>(ClientMessageType::INPUT);
    buf[1] = buttons;
    std::memcpy(buf.data() + 2, &yaw,   sizeof(float));
    std::memcpy(buf.data() + 6, &pitch, sizeof(float));
    return buf;
}

/** Entity state parsed from a TICK_DELTA entity section. */
struct ParsedEntity {
    uint16_t id{};
    uint8_t  type{};
    int32_t  x{}, y{}, z{};
    int32_t  vx{}, vy{}, vz{};
    bool     grounded{};
};

/**
 * Parse entities from the payload of an uncompressed TICK_DELTA message.
 * Layout (after the 13-byte header):
 *   int32 voxel_count
 *   repeat: VoxelId(2) + VoxelType(1)
 *   int32 entity_count
 *   repeat: DeltaType(1) + ChunkEntityId(2) + EntityType(1) + Flags(1) + [fields]
 */
static std::vector<ParsedEntity> parseTickDeltaEntities(const uint8_t* msg, size_t len)
{
    std::vector<ParsedEntity> result;
    if (len < 13) return result;

    // Skip 13-byte header
    const uint8_t* p   = msg + 13;
    const uint8_t* end = msg + len;

    if (p + 4 > end) return result;
    int32_t voxelCount = 0;
    std::memcpy(&voxelCount, p, 4); p += 4;
    p += static_cast<ptrdiff_t>(voxelCount) * 3;  // VoxelId(2)+VoxelType(1)

    if (p + 4 > end) return result;
    int32_t entityCount = 0;
    std::memcpy(&entityCount, p, 4); p += 4;

    for (int i = 0; i < entityCount && p < end; ++i) {
        if (p + 5 > end) break;
        /* uint8_t  deltaType = */ ++p;  // skip DeltaType
        uint16_t entityId = 0;
        std::memcpy(&entityId, p, 2); p += 2;
        uint8_t entityType = *p++;
        uint8_t flags      = *p++;

        if ((flags & POSITION_BIT) == 0 || p + 25 > end) continue;
        ParsedEntity e{};
        e.id   = entityId;
        e.type = entityType;
        std::memcpy(&e.x,  p,      4);
        std::memcpy(&e.y,  p + 4,  4);
        std::memcpy(&e.z,  p + 8,  4);
        std::memcpy(&e.vx, p + 12, 4);
        std::memcpy(&e.vy, p + 16, 4);
        std::memcpy(&e.vz, p + 20, 4);
        e.grounded = p[24] != 0;
        p += 25;
        result.push_back(e);
    }
    return result;
}

/**
 * Collect parsed entities from all uncompressed TICK_DELTA or SNAPSHOT_DELTA
 * messages in a length-prefixed batch.  Compressed variants are skipped (they
 * only appear for large payloads that won't occur in unit tests).
 */
static std::vector<ParsedEntity> collectFromBatch(const uint8_t* data, size_t size)
{
    std::vector<ParsedEntity> result;
    size_t off = 0;
    while (off + 4 <= size) {
        uint32_t msgLen = 0;
        std::memcpy(&msgLen, data + off, 4); off += 4;
        if (off + msgLen > size) break;

        const uint8_t* msg = data + off;
        if (msgLen >= 13) {
            const auto t = msg[0];
            if (t == static_cast<uint8_t>(ChunkMessageType::TICK_DELTA) ||
                t == static_cast<uint8_t>(ChunkMessageType::SNAPSHOT_DELTA))
            {
                auto ents = parseTickDeltaEntities(msg, msgLen);
                result.insert(result.end(), ents.begin(), ents.end());
            }
        }
        off += msgLen;
    }
    return result;
}

// ── GHOST physics ──────────────────────────────────────────────────────────

TEST_CASE("stepPhysics - GHOST player at rest produces no tick-delta entity update",
          "[physics][ghost]")
{
    GameEngine engine;
    engine.registerGateway(1);
    engine.addPlayer(1, 1, 0.0f, 8.0f, 0.0f);  // GHOST_PLAYER by default

    std::vector<ParsedEntity> states;
    engine.setOutputCallback([&](GatewayId, const uint8_t* data, size_t size) {
        auto s = collectFromBatch(data, size);
        states.insert(states.end(), s.begin(), s.end());
    });

    // Zero input → velocity stays 0 → InputSystem does not mark dirty
    const auto input = makeInput(0);
    engine.handlePlayerInput(1, input.data(), input.size());

    states.clear();
    engine.tick();

    // GHOST with vx=vy=vz=0 is skipped by stepPhysics; no dirty flag → no entity delta.
    REQUIRE(states.empty());
}

TEST_CASE("stepPhysics - GHOST player moves forward one tick (yaw=0)",
          "[physics][ghost]")
{
    GameEngine engine;
    engine.registerGateway(1);
    engine.addPlayer(1, 1, 0.0f, 8.0f, 0.0f);

    std::vector<ParsedEntity> states;
    engine.setOutputCallback([&](GatewayId, const uint8_t* data, size_t size) {
        auto s = collectFromBatch(data, size);
        states.insert(states.end(), s.begin(), s.end());
    });

    // Warmup tick 0 (tickCount % SNAPSHOT_DELTA_INTERVAL == 0 → snapshotDelta).
    // Zero input ensures no dirty flag so no entity in the delta.
    engine.handlePlayerInput(1, makeInput(0).data(), 10);
    engine.tick();

    // FORWARD at yaw=0, pitch=0:
    //   InputSystem: dz += -cos(0)*cos(0) = -1, len=1, nvz = -GHOST_MOVE_SPEED = -256
    // Tick 1 → tickDelta path (1 % 10 != 0).
    const uint8_t fwd = static_cast<uint8_t>(InputButton::FORWARD);
    engine.handlePlayerInput(1, makeInput(fwd, 0.0f, 0.0f).data(), 10);

    states.clear();
    engine.tick();

    // Velocity changed → InputSystem marks dirty → tick delta contains entity update.
    REQUIRE(!states.empty());
    const ParsedEntity& e = states[0];

    // After one tick: z = 0 + vz*1 = -GHOST_MOVE_SPEED sub-voxels
    CHECK(e.vz == -static_cast<int32_t>(GHOST_MOVE_SPEED));
    CHECK(e.z  == -static_cast<int32_t>(GHOST_MOVE_SPEED));
    CHECK(e.vx == 0);
    CHECK(e.x  == 0);
    CHECK(e.grounded == true);  // GHOST always grounded
}

TEST_CASE("stepPhysics - GHOST player moves diagonally (yaw=0 FORWARD+RIGHT)",
          "[physics][ghost]")
{
    GameEngine engine;
    engine.registerGateway(1);
    engine.addPlayer(1, 1, 0.0f, 8.0f, 0.0f);

    std::vector<ParsedEntity> states;
    engine.setOutputCallback([&](GatewayId, const uint8_t* data, size_t size) {
        auto s = collectFromBatch(data, size);
        states.insert(states.end(), s.begin(), s.end());
    });

    // Warmup tick 0.
    engine.handlePlayerInput(1, makeInput(0).data(), 10);
    engine.tick();

    // FORWARD + RIGHT at yaw=0, pitch=0:
    //   dz += -cos(0)*cos(0) = -1  (FORWARD)
    //   dx += cos(0) = +1           (RIGHT)
    //   len = sqrt(2), nvx = round(1/sqrt(2)*256), nvz = round(-1/sqrt(2)*256)
    const uint8_t btns = static_cast<uint8_t>(InputButton::FORWARD)
                       | static_cast<uint8_t>(InputButton::RIGHT);
    engine.handlePlayerInput(1, makeInput(btns, 0.0f, 0.0f).data(), 10);

    states.clear();
    engine.tick();

    REQUIRE(!states.empty());
    const ParsedEntity& e = states[0];

    // Both axes must be non-zero and magnitude must equal GHOST_MOVE_SPEED
    const double speed = std::sqrt(static_cast<double>(e.vx) * e.vx +
                                   static_cast<double>(e.vz) * e.vz);
    CHECK(e.vx > 0);
    CHECK(e.vz < 0);
    // Speed should be within 1 sub-voxel of GHOST_MOVE_SPEED (rounding in int cast)
    CHECK(speed >= GHOST_MOVE_SPEED - 1);
    CHECK(speed <= GHOST_MOVE_SPEED + 1);
    CHECK(e.grounded == true);
}

// ── FULL physics ───────────────────────────────────────────────────────────

TEST_CASE("stepPhysics - FULL player falls silently (no delta while airborne, no collision)",
          "[physics][full]")
{
    GameEngine engine;
    engine.registerGateway(1);
    // Spawn high above any terrain; cy ≈ 6 (y=100 voxels → 100/16=6.25)
    engine.addPlayer(1, 1, 0.0f, 100.0f, 0.0f, EntityType::PLAYER);

    std::vector<ParsedEntity> states;
    engine.setOutputCallback([&](GatewayId, const uint8_t* data, size_t size) {
        auto s = collectFromBatch(data, size);
        states.insert(states.end(), s.begin(), s.end());
    });

    const auto zeroInput = makeInput(0);
    engine.handlePlayerInput(1, zeroInput.data(), zeroInput.size());

    // First tick: vy goes 0 → -GRAVITY_DECREMENT = -6.
    // grounded does not change (false → false), no horizontal collision → no dirty.
    states.clear();
    engine.tick();
    // Client predicts correctly via the same gravity formula — no delta needed.
    REQUIRE(states.empty());
}

TEST_CASE("stepPhysics - FULL player eventually lands and becomes grounded",
          "[physics][full]")
{
    GameEngine engine;
    engine.registerGateway(1);
    engine.addPlayer(1, 1, 0.0f, 100.0f, 0.0f, EntityType::PLAYER);

    std::vector<ParsedEntity> states;
    engine.setOutputCallback([&](GatewayId, const uint8_t* data, size_t size) {
        auto s = collectFromBatch(data, size);
        states.insert(states.end(), s.begin(), s.end());
    });

    const auto zeroInput = makeInput(0);
    engine.handlePlayerInput(1, zeroInput.data(), zeroInput.size());

    // Run until grounded delta is received or we time out (200 ticks ≈ 10 s).
    bool landed = false;
    for (int t = 0; t < 200 && !landed; ++t) {
        states.clear();
        engine.tick();
        for (const auto& e : states) {
            if (e.grounded) { landed = true; break; }
        }
    }

    REQUIRE(landed);

    // On landing: vy is zeroed and grounded = true.
    const auto it = std::find_if(states.begin(), states.end(),
                                 [](const ParsedEntity& e) { return e.grounded; });
    REQUIRE(it != states.end());
    CHECK(it->vy == 0);
    // Y position should be above 0 (landed on terrain, not fallen through the world)
    CHECK(it->y > 0);
}

TEST_CASE("stepPhysics - FULL player jump impulse when grounded",
          "[physics][full]")
{
    GameEngine engine;
    engine.registerGateway(1);
    // Spawn above the terrain surface (terrain surface ≤ 30 voxels globally in cy=0).
    // y=50 ensures the player is in free air and will land cleanly on terrain.
    engine.addPlayer(1, 1, 0.0f, 50.0f, 0.0f, EntityType::PLAYER);

    std::vector<ParsedEntity> states;
    engine.setOutputCallback([&](GatewayId, const uint8_t* data, size_t size) {
        auto s = collectFromBatch(data, size);
        states.insert(states.end(), s.begin(), s.end());
    });

    // Let the player land first (falls ~30 voxels in ~50 ticks).
    const auto zeroInput = makeInput(0);
    engine.handlePlayerInput(1, zeroInput.data(), zeroInput.size());
    for (int t = 0; t < 200; ++t) {
        states.clear();
        engine.tick();
        bool gr = false;
        for (const auto& e : states) if (e.grounded) { gr = true; break; }
        if (gr) break;
    }

    // Now send JUMP input.
    const uint8_t jumpBtn = static_cast<uint8_t>(InputButton::JUMP);
    engine.handlePlayerInput(1, makeInput(jumpBtn).data(), 10);

    states.clear();
    engine.tick();

    // Jump should produce an entity delta with positive vy (PLAYER_JUMP_VY)
    // and grounded = false.
    bool foundJump = false;
    for (const auto& e : states) {
        if (e.vy > 0) { foundJump = true; break; }
    }
    // Note: jump only fires when grounded. If the player landed in previous loop,
    // foundJump must be true.
    CHECK(foundJump);
}
