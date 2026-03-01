#include "game/WorldChunk.hpp"
#include <cstring>
#include <cmath>

namespace {

// Hash-based value noise — no external dependency needed.

static uint32_t hash2(int32_t x, int32_t z) noexcept {
    uint32_t h = static_cast<uint32_t>(x) * 1619u ^ static_cast<uint32_t>(z) * 31337u;
    h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16;
    return h;
}

static float gridVal(int32_t x, int32_t z) noexcept {
    return (hash2(x, z) & 0xFFFF) / 65536.0f;  // [0, 1)
}

static float smooth(float t) noexcept { return t * t * (3.0f - 2.0f * t); }

static float valueNoise(float x, float z) noexcept {
    const int32_t ix = static_cast<int32_t>(std::floor(x));
    const int32_t iz = static_cast<int32_t>(std::floor(z));
    const float fx = smooth(x - static_cast<float>(ix));
    const float fz = smooth(z - static_cast<float>(iz));
    const float v00 = gridVal(ix,   iz),   v10 = gridVal(ix+1, iz);
    const float v01 = gridVal(ix, iz+1),   v11 = gridVal(ix+1, iz+1);
    return v00 + (v10-v00)*fx + (v01-v00)*fz + (v00+v11-v10-v01)*fx*fz;
}

} // anonymous namespace

namespace voxelmmo {

WorldChunk::WorldChunk() {
    voxels.assign(CHUNK_VOXEL_COUNT, 0);
}

void WorldChunk::generate(int32_t cx, int8_t cy, int32_t cz) {
    // Height-mapped terrain — surface within cy == 0, stone below, air above.
    // Two noise octaves: large hills (period ~48) + small bumps (period ~12).
    for (uint8_t x = 0; x < CHUNK_SIZE_X; ++x) {
        for (uint8_t z = 0; z < CHUNK_SIZE_Z; ++z) {
            const float wx = static_cast<float>(cx * CHUNK_SIZE_X + x);
            const float wz = static_cast<float>(cz * CHUNK_SIZE_Z + z);
            const float n  = valueNoise(wx / 48.0f, wz / 48.0f) * 8.0f
                           + valueNoise(wx / 12.0f, wz / 12.0f) * 2.0f;
            // Surface world-Y in [4, 14] — always inside the cy == 0 layer.
            const int32_t surfaceY = 4 + static_cast<int32_t>(n);

            for (uint8_t y = 0; y < CHUNK_SIZE_Y; ++y) {
                const int32_t worldY = static_cast<int32_t>(cy) * CHUNK_SIZE_Y + y;
                VoxelType type;
                if      (worldY > surfaceY)        type = VoxelTypes::AIR;
                else if (worldY == surfaceY)        type = VoxelTypes::GRASS;
                else if (worldY >= surfaceY - 3)   type = VoxelTypes::DIRT;
                else                               type = VoxelTypes::STONE;

                voxels[static_cast<size_t>(y) * CHUNK_SIZE_X * CHUNK_SIZE_Z
                     + static_cast<size_t>(x) * CHUNK_SIZE_Z
                     + static_cast<size_t>(z)] = type;
            }
        }
    }
}

void WorldChunk::modifyVoxels(const std::vector<std::pair<VoxelId, VoxelType>>& mods) {
    for (const auto& [vid, vtype] : mods) {
        voxels[indexOf(vid)] = vtype;
        // TODO: check that vid is not already present
        voxelsSnapshotDeltas.emplace_back(vid, vtype);
        voxelsTickDeltas.emplace_back(vid, vtype);
    }
}

size_t WorldChunk::serializeSnapshot(uint8_t* buf) const {
    size_t off = 0;
    const int32_t count = static_cast<int32_t>(CHUNK_VOXEL_COUNT);
    std::memcpy(buf + off, &count, sizeof(int32_t));
    off += sizeof(int32_t);
    std::memcpy(buf + off, voxels.data(), CHUNK_VOXEL_COUNT);
    off += CHUNK_VOXEL_COUNT;
    return off;
}

static size_t serializeDeltaImpl(
    const std::vector<std::pair<VoxelId, VoxelType>>& deltas,
    uint8_t* buf)
{
    size_t off = 0;
    const int32_t count = static_cast<int32_t>(deltas.size());
    std::memcpy(buf + off, &count, sizeof(int32_t));
    off += sizeof(int32_t);
    for (const auto& [vid, vtype] : deltas) {
        std::memcpy(buf + off, &vid.packed, sizeof(uint16_t));
        off += sizeof(uint16_t);
        buf[off++] = vtype;
    }
    return off;
}

size_t WorldChunk::serializeSnapshotDelta(uint8_t* buf) const {
    return serializeDeltaImpl(voxelsSnapshotDeltas, buf);
}

size_t WorldChunk::serializeTickDelta(uint8_t* buf) const {
    return serializeDeltaImpl(voxelsTickDeltas, buf);
}

void WorldChunk::clearSnapshotDelta() { voxelsSnapshotDeltas.clear(); }
void WorldChunk::clearTickDelta()     { voxelsTickDeltas.clear();     }

} // namespace voxelmmo
