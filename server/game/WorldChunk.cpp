#include "game/WorldChunk.hpp"
#include "common/VoxelCatalog.hpp"
#include <cstring>

namespace voxelmmo {

WorldChunk::WorldChunk() {
    voxels.assign(CHUNK_VOXEL_COUNT, 0);
    voxelPhysicTypes.assign(CHUNK_VOXEL_COUNT, VoxelPhysicTypes::AIR);
}

void WorldChunk::setVoxel(uint32_t voxelX, uint32_t voxelY, uint32_t voxelZ, VoxelType type) {
    const VoxelIndex idx = voxelIndexFromPos(voxelX, voxelY, voxelZ);
    voxels[idx] = type;
    voxelPhysicTypes[idx] = toVoxelPhysicType(type);
    // TODO: check that idx is not already present in deltas
    voxelsDeltas.emplace_back(idx, type);
}

void WorldChunk::modifyVoxels(const std::vector<std::pair<VoxelIndex, VoxelType>>& mods) {
    for (const auto& [idx, vtype] : mods) {
        voxels[idx] = vtype;
        voxelPhysicTypes[idx] = toVoxelPhysicType(vtype);
        // TODO: check that idx is not already present
        voxelsDeltas.emplace_back(idx, vtype);
    }
}

void WorldChunk::rebuildPhysicTypeCache() {
    voxelPhysicTypes.resize(CHUNK_VOXEL_COUNT);
    for (size_t i = 0; i < CHUNK_VOXEL_COUNT; ++i) {
        voxelPhysicTypes[i] = toVoxelPhysicType(voxels[i]);
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

size_t WorldChunk::serializeDelta(uint8_t* buf) const {
    return serializeDeltaImpl(voxelsDeltas, buf);
}

void WorldChunk::clearDelta() { voxelsDeltas.clear(); }

} // namespace voxelmmo
