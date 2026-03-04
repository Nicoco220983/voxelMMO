#pragma once
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include <vector>
#include <utility>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Stores the voxel data for one chunk and manages serialisation of
 *        snapshot / snapshot-delta / tick-delta messages.
 *
 * The serialisation wire format for the voxel section is:
 *   Snapshot:      int32 count, [VoxelType * CHUNK_VOXEL_COUNT]
 *   Delta:         int32 count, [(VoxelIndex uint16, VoxelType) * count]
 *                  VoxelIndex = packVoxelIndex(x, y, z)
 */
class WorldChunk {
public:

    /** @brief All voxels in row-major (y, x, z) order. Size = CHUNK_VOXEL_COUNT.
     *         Use packVoxelIndex(x, y, z) to compute the index. */
    std::vector<VoxelType> voxels;

    /** @brief Accumulated voxel changes since the last snapshot was sent. */
    std::vector<std::pair<VoxelIndex, VoxelType>> voxelsSnapshotDeltas;

    /** @brief Voxel changes in the current tick only. */
    std::vector<std::pair<VoxelIndex, VoxelType>> voxelsTickDeltas;

    WorldChunk();

    /** @brief Procedurally fill voxel data for a given chunk position. */
    void generate(int32_t chunkX, int8_t chunkY, int32_t chunkZ);

    /** @brief Get voxel type at local chunk coordinates. */
    VoxelType getVoxel(uint32_t voxelX, uint32_t voxelY, uint32_t voxelZ) const {
        return voxels[packVoxelIndex(voxelX, voxelY, voxelZ)];
    }

    /** @brief Set voxel type at local chunk coordinates and record as delta. */
    void setVoxel(uint32_t voxelX, uint32_t voxelY, uint32_t voxelZ, VoxelType type);

    /**
     * @brief Apply a batch of voxel modifications (via packed indices) and record as deltas.
     * @param mods  List of (VoxelIndex, VoxelType) pairs.
     */
    void modifyVoxels(const std::vector<std::pair<VoxelIndex, VoxelType>>& mods);

    /**
     * @brief Write the full voxel snapshot into buf.
     * @return Number of bytes written.
     */
    size_t serializeSnapshot(uint8_t* buf) const;

    /**
     * @brief Write the snapshot voxel delta into buf.
     * @return Number of bytes written.
     */
    size_t serializeSnapshotDelta(uint8_t* buf) const;

    /**
     * @brief Write the tick voxel delta into buf.
     * @return Number of bytes written.
     */
    size_t serializeTickDelta(uint8_t* buf) const;

    /** @brief Clear snapshot delta accumulator (call after a snapshot delta is dispatched). */
    void clearSnapshotDelta();

    /** @brief Clear tick delta accumulator (call at the end of every tick). */
    void clearTickDelta();

};

} // namespace voxelmmo
