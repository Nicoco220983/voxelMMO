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
 *   Delta:         int32 count, [(VoxelId uint16, VoxelType) * count]
 */
class WorldChunk {
public:

    /** @brief All voxels in row-major (y, x, z) order. Size = CHUNK_VOXEL_COUNT. */
    std::vector<VoxelType> voxels;

    /** @brief Accumulated voxel changes since the last snapshot was sent. */
    std::vector<std::pair<VoxelId, VoxelType>> voxelsSnapshotDeltas;

    /** @brief Voxel changes in the current tick only. */
    std::vector<std::pair<VoxelId, VoxelType>> voxelsTickDeltas;

    WorldChunk();

    /**
     * @brief Procedurally fill voxel data for a given chunk position.
     * @param cx  Chunk X coordinate.
     * @param cy  Chunk Y coordinate (vertical layer).
     * @param cz  Chunk Z coordinate.
     */
    void generate(int32_t cx, int8_t cy, int32_t cz);

    /**
     * @brief Apply a batch of voxel modifications and record them as deltas.
     * @param mods  List of (VoxelId, VoxelType) pairs.
     */
    void modifyVoxels(const std::vector<std::pair<VoxelId, VoxelType>>& mods);

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

    /** @brief Flat index into voxels[] from a VoxelId. */
    static constexpr size_t indexOf(VoxelId vid) noexcept {
        return static_cast<size_t>(vid.y()) * CHUNK_SIZE_X * CHUNK_SIZE_Z
             + static_cast<size_t>(vid.x()) * CHUNK_SIZE_Z
             + static_cast<size_t>(vid.z());
    }
};

} // namespace voxelmmo
