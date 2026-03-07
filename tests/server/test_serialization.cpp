#include <catch2/catch_test_macros.hpp>
#include "common/ChunkState.hpp"
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include "game/WorldChunk.hpp"
#include "game/WorldGenerator.hpp"

#include <cstring>
#include <vector>

using namespace voxelmmo;

// Helper: generate chunk using WorldGenerator
static void generateChunk(WorldChunk& chunk, int cx, int cy, int cz) {
    WorldGenerator gen;
    gen.generate(chunk.voxels, cx, cy, cz);
}

// ── Helpers ────────────────────────────────────────────────────────────────

// Build a minimal 15-byte fake delta/snapshot header with the given tick value.
// Format: [type(1)][size(2)][chunkId(8)][tick(4)]
static std::vector<uint8_t> fakeMsg(uint32_t tick, size_t totalSize = 15) {
    std::vector<uint8_t> msg(totalSize, 0);
    // tick is now at offset 11 in the 15-byte header
    std::memcpy(msg.data() + 11, &tick, sizeof(uint32_t));
    // size field at bytes 1-2
    if (totalSize >= 3) {
        msg[1] = static_cast<uint8_t>(totalSize & 0xFF);
        msg[2] = static_cast<uint8_t>((totalSize >> 8) & 0xFF);
    }
    return msg;
}

// ── ChunkState tests ───────────────────────────────────────────────────────

TEST_CASE("ChunkState - empty state: deltasNewerThan returns empty range", "[chunkstate]") {
    ChunkState state;
    auto [b, e] = state.deltasNewerThan(0);
    REQUIRE(b == e);
}

TEST_CASE("ChunkState::receiveDelta - single delta", "[chunkstate]") {
    ChunkState state;
    auto msg = fakeMsg(5);
    state.receiveDelta(msg.data(), msg.size());

    REQUIRE(state.hasNewDelta);
    REQUIRE(state.deltaOffsets.size() == 1);
    REQUIRE(state.deltaOffsets[0].tick == 5);
    REQUIRE(state.deltaOffsets[0].offset == 0);
    REQUIRE(state.deltas.size() == 15);

    // Nothing newer than tick 5
    auto [b1, e1] = state.deltasNewerThan(5);
    REQUIRE(b1 == e1);

    // Everything newer than tick 4
    auto [b2, e2] = state.deltasNewerThan(4);
    REQUIRE(b2 == 0);
    REQUIRE(e2 == 15);
}

TEST_CASE("ChunkState::receiveDelta - multiple deltas, deltasNewerThan filters", "[chunkstate]") {
    ChunkState state;

    auto d1 = fakeMsg(3, 20);  // 20-byte message at tick 3
    auto d2 = fakeMsg(7, 15);  // 15-byte message at tick 7

    state.receiveDelta(d1.data(), d1.size());
    state.receiveDelta(d2.data(), d2.size());

    REQUIRE(state.deltas.size() == 35);
    REQUIRE(state.deltaOffsets.size() == 2);

    // Newer than tick 3 → only d2 (offset 20, size 15)
    auto [b, e] = state.deltasNewerThan(3);
    REQUIRE(b == 20);
    REQUIRE(e == 35);

    // Newer than tick 7 → nothing
    auto [b2, e2] = state.deltasNewerThan(7);
    REQUIRE(b2 == e2);

    // Newer than tick 0 → both
    auto [b3, e3] = state.deltasNewerThan(0);
    REQUIRE(b3 == 0);
    REQUIRE(e3 == 35);
}

TEST_CASE("ChunkState::receiveSnapshot - stores data and clears deltas", "[chunkstate]") {
    ChunkState state;

    // Accumulate some deltas first
    auto d = fakeMsg(2);
    state.receiveDelta(d.data(), d.size());
    REQUIRE(!state.deltas.empty());

    // Now receive a snapshot at tick 10
    auto snap = fakeMsg(10);
    state.receiveSnapshot(snap.data(), snap.size());

    REQUIRE(state.snapshotTick == 10);
    REQUIRE(state.snapshot.size() == 13);
    REQUIRE(state.deltas.empty());
    REQUIRE(state.deltaOffsets.empty());
    REQUIRE(!state.hasNewDelta);
}

TEST_CASE("ChunkState - delta data is preserved verbatim", "[chunkstate]") {
    ChunkState state;

    // Build a delta with a recognisable payload
    std::vector<uint8_t> msg(20, 0xAB);
    uint32_t tick = 99;
    std::memcpy(msg.data() + 9, &tick, sizeof(uint32_t));

    state.receiveDelta(msg.data(), msg.size());

    REQUIRE(std::memcmp(state.deltas.data(), msg.data(), msg.size()) == 0);
}

// ── WorldChunk serialization tests ───────────────────────────────────────────

TEST_CASE("WorldChunk::serializeSnapshot - size and count field", "[serialization]") {
    WorldChunk chunk;
    generateChunk(chunk, 0, 0, 0);

    std::vector<uint8_t> buf(sizeof(int32_t) + CHUNK_VOXEL_COUNT);
    size_t written = chunk.serializeSnapshot(buf.data());

    REQUIRE(written == sizeof(int32_t) + CHUNK_VOXEL_COUNT);

    int32_t count = 0;
    std::memcpy(&count, buf.data(), sizeof(int32_t));
    REQUIRE(count == static_cast<int32_t>(CHUNK_VOXEL_COUNT));
}

TEST_CASE("WorldChunk::serializeSnapshot - voxel data roundtrip", "[serialization]") {
    WorldChunk chunk;
    generateChunk(chunk, 3, 0, -5);

    std::vector<uint8_t> buf(sizeof(int32_t) + CHUNK_VOXEL_COUNT);
    chunk.serializeSnapshot(buf.data());

    // Voxel bytes after the 4-byte count header must match the live voxels
    REQUIRE(std::memcmp(buf.data() + sizeof(int32_t),
                        chunk.voxels.data(),
                        CHUNK_VOXEL_COUNT) == 0);
}

TEST_CASE("WorldChunk::modifyVoxels + serializeTickDelta roundtrip", "[serialization]") {
    WorldChunk chunk;
    generateChunk(chunk, 0, 0, 0);

    const VoxelIndex idx0 = packVoxelIndex(10, 5, 20);
    const VoxelIndex idx1 = packVoxelIndex(7, 3, 7);
    chunk.modifyVoxels({{idx0, VoxelTypes::STONE}, {idx1, VoxelTypes::AIR}});

    REQUIRE(chunk.voxelsTickDeltas.size() == 2);

    // Buffer: int32 count + 2 × (uint16 VoxelIndex + uint8 VoxelType)
    const size_t expectedSize = sizeof(int32_t) + 2 * (sizeof(VoxelIndex) + sizeof(uint8_t));
    std::vector<uint8_t> buf(expectedSize);
    size_t written = chunk.serializeTickDelta(buf.data());
    REQUIRE(written == expectedSize);

    int32_t count = 0;
    std::memcpy(&count, buf.data(), sizeof(int32_t));
    REQUIRE(count == 2);

    // First entry
    VoxelIndex ridx0 = 0;
    std::memcpy(&ridx0, buf.data() + sizeof(int32_t), sizeof(VoxelIndex));
    REQUIRE(ridx0 == idx0);
    REQUIRE(buf[sizeof(int32_t) + sizeof(VoxelIndex)] == VoxelTypes::STONE);

    // Second entry
    const size_t entry1 = sizeof(int32_t) + sizeof(VoxelIndex) + sizeof(uint8_t);
    VoxelIndex ridx1 = 0;
    std::memcpy(&ridx1, buf.data() + entry1, sizeof(VoxelIndex));
    REQUIRE(ridx1 == idx1);
    REQUIRE(buf[entry1 + sizeof(VoxelIndex)] == VoxelTypes::AIR);
}

TEST_CASE("WorldChunk::clearTickDelta empties tick deltas, preserves snapshot deltas", "[serialization]") {
    WorldChunk chunk;
    generateChunk(chunk, 0, 0, 0);
    chunk.modifyVoxels({{packVoxelIndex(0, 0, 0), VoxelTypes::STONE}});

    REQUIRE(!chunk.voxelsTickDeltas.empty());
    REQUIRE(!chunk.voxelsSnapshotDeltas.empty());

    chunk.clearTickDelta();
    REQUIRE(chunk.voxelsTickDeltas.empty());
    REQUIRE(!chunk.voxelsSnapshotDeltas.empty());  // snapshot delta still present
}

TEST_CASE("WorldChunk::clearSnapshotDelta empties snapshot deltas only", "[serialization]") {
    WorldChunk chunk;
    generateChunk(chunk, 0, 0, 0);
    chunk.modifyVoxels({{packVoxelIndex(2, 1, 3), VoxelTypes::GRASS}});

    chunk.clearSnapshotDelta();
    REQUIRE(chunk.voxelsSnapshotDeltas.empty());
    REQUIRE(!chunk.voxelsTickDeltas.empty());  // tick delta still present
}

TEST_CASE("VoxelIndex pack/unpack roundtrip", "[types]") {
    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x : {0u, 1u, 15u, 31u}) {
            for (uint32_t z : {0u, 1u, 15u, 31u}) {
                const VoxelIndex idx = packVoxelIndex(x, y, z);
                uint32_t ux, uy, uz;
                unpackVoxelIndex(idx, ux, uy, uz);
                REQUIRE(uy == y);
                REQUIRE(ux == x);
                REQUIRE(uz == z);
            }
        }
    }
}

TEST_CASE("ChunkId packing roundtrip", "[types]") {
    for (int32_t y : {-32, -1, 0, 1, 31}) {
        for (int32_t x : {-268435456, -1, 0, 1, 268435455}) {
            for (int32_t z : {-268435456, -1, 0, 1, 268435455}) {
                const ChunkId cid = ChunkId::make(y, x, z);
                REQUIRE(cid.y() == y);
                REQUIRE(cid.x() == x);
                REQUIRE(cid.z() == z);
            }
        }
    }
}
