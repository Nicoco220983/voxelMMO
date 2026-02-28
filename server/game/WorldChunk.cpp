#include "game/WorldChunk.hpp"
#include <cstring>

namespace voxelmmo {

WorldChunk::WorldChunk() {
    voxels.assign(CHUNK_VOXEL_COUNT, 0);
}

void WorldChunk::generate(int32_t /*cx*/, int8_t cy, int32_t /*cz*/) {
    // Simple flat-terrain generation:
    //   cy < 0  → full stone
    //   cy == 0 → bottom 8 layers stone, layers 8–14 dirt, layer 15 grass
    //   cy > 0  → all air
    for (uint8_t y = 0; y < CHUNK_SIZE_Y; ++y) {
        VoxelType type = VoxelTypes::AIR;
        if (cy < 0) {
            type = VoxelTypes::STONE;
        } else if (cy == 0) {
            if      (y < 8)  type = VoxelTypes::STONE;
            else if (y < 15) type = VoxelTypes::DIRT;
            else             type = VoxelTypes::GRASS;
        }
        const size_t row = static_cast<size_t>(y) * CHUNK_SIZE_X * CHUNK_SIZE_Z;
        std::fill(voxels.begin() + static_cast<ptrdiff_t>(row),
                  voxels.begin() + static_cast<ptrdiff_t>(row + CHUNK_SIZE_X * CHUNK_SIZE_Z),
                  type);
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
