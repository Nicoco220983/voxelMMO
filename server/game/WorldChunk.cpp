#include "game/WorldChunk.hpp"
#include "game/WorldGenerator.hpp"
#include <cstring>

namespace voxelmmo {

WorldChunk::WorldChunk() {
    voxels.assign(CHUNK_VOXEL_COUNT, 0);
}

void WorldChunk::generate(int32_t chunkX, int8_t chunkY, int32_t chunkZ) {
    WorldGenerator{}.generate(voxels, chunkX, chunkY, chunkZ);
}

void WorldChunk::setVoxel(uint32_t voxelX, uint32_t voxelY, uint32_t voxelZ, VoxelType type) {
    const VoxelIndex idx = packVoxelIndex(voxelX, voxelY, voxelZ);
    voxels[idx] = type;
    // TODO: check that idx is not already present in deltas
    voxelsSnapshotDeltas.emplace_back(idx, type);
    voxelsTickDeltas.emplace_back(idx, type);
}

void WorldChunk::modifyVoxels(const std::vector<std::pair<VoxelIndex, VoxelType>>& mods) {
    for (const auto& [idx, vtype] : mods) {
        voxels[idx] = vtype;
        // TODO: check that idx is not already present
        voxelsSnapshotDeltas.emplace_back(idx, vtype);
        voxelsTickDeltas.emplace_back(idx, vtype);
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
    const std::vector<std::pair<VoxelIndex, VoxelType>>& deltas,
    uint8_t* buf)
{
    size_t off = 0;
    const int32_t count = static_cast<int32_t>(deltas.size());
    std::memcpy(buf + off, &count, sizeof(int32_t));
    off += sizeof(int32_t);
    for (const auto& [idx, vtype] : deltas) {
        std::memcpy(buf + off, &idx, sizeof(VoxelIndex));
        off += sizeof(VoxelIndex);
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
