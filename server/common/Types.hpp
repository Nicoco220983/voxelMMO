#pragma once
#include <cstdint>
#include <functional>
#include <iostream>

namespace voxelmmo {

// ── Strong coordinate type aliases for documentation/clarity ────────────────

/** @brief Sub-voxel coordinate (1 voxel = 256 sub-voxel units). */
using SubVoxelCoord = int32_t;

/** @brief Voxel coordinate (world-space in voxels). */
using VoxelCoord = int32_t;

/** @brief Chunk coordinate. */
using ChunkCoord = int32_t;

// ── Chunk voxel dimensions (must match VoxelIndex bit widths) ─────────────────
inline constexpr uint8_t CHUNK_SIZE_Y = 32;   ///< uint5 range [0,31]
inline constexpr uint8_t CHUNK_SIZE_X = 32;   ///< uint5 range [0,31]
inline constexpr uint8_t CHUNK_SIZE_Z = 32;   ///< uint5 range [0,31]
inline constexpr size_t  CHUNK_VOXEL_COUNT = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z; // 32 768

/// Sub-voxel precision: 1 voxel = 256 position units.
inline constexpr int     SUBVOXEL_BITS = 8;
inline constexpr int32_t SUBVOXEL_SIZE = 1 << SUBVOXEL_BITS;  ///< 256
inline constexpr int32_t SUBVOXEL_MASK = SUBVOXEL_SIZE - 1;   ///< 0xFF

/// Bit-shift from sub-voxel position to chunk coordinate = log2(chunk_dim × SUBVOXEL_SIZE).
inline constexpr int CHUNK_SHIFT_Y = 13;  ///< log2(CHUNK_SIZE_Y × SUBVOXEL_SIZE) = log2(32 × 256)
inline constexpr int CHUNK_SHIFT_X = 13;  ///< log2(CHUNK_SIZE_X × SUBVOXEL_SIZE) = log2(32 × 256)
inline constexpr int CHUNK_SHIFT_Z = 13;  ///< log2(CHUNK_SIZE_Z × SUBVOXEL_SIZE) = log2(32 × 256)

/**
 * @brief Chunk identifier packed as sint8(y) | sint28(x) | sint28(z) into 64 bits.
 *
 * Bit layout (MSB first): [63:56] y (8-bit signed) | [55:28] x (28-bit signed) | [27:0] z (28-bit signed)
 *
 * World extents: y ∈ [-128, 127], x/z ∈ [-134 217 728, 134 217 727]
 */
struct ChunkId {
    int64_t packed{0};

    /** @brief Construct a ChunkId from its three signed components. */
    static constexpr ChunkId make(ChunkCoord chunkY, ChunkCoord chunkX, ChunkCoord chunkZ) noexcept {
        ChunkId id;
        id.packed = (static_cast<int64_t>(chunkY  & 0xFF)       << 56)
                  | (static_cast<int64_t>(chunkX  & 0xFFFFFFF)  << 28)
                  |  static_cast<int64_t>(chunkZ  & 0xFFFFFFF);
        return id;
    }

    /** @brief Create a ChunkId from its three signed chunk coordinates.
     *  Matches client-side naming: chunkIdFromChunkPos()
     */
    static constexpr ChunkId fromChunkPos(ChunkCoord chunkX, ChunkCoord chunkY, ChunkCoord chunkZ) noexcept {
        return make(chunkY, chunkX, chunkZ);
    }

    /** @brief ChunkId for the chunk that contains voxel coordinates (vx, vy, vz).
     *  Matches client-side naming: chunkIdFromVoxelPos()
     */
    static constexpr ChunkId fromVoxelPos(VoxelCoord voxelX, VoxelCoord voxelY, VoxelCoord voxelZ) noexcept {
        return make(
            voxelY >> (CHUNK_SHIFT_Y - SUBVOXEL_BITS),
            voxelX >> (CHUNK_SHIFT_X - SUBVOXEL_BITS),
            voxelZ >> (CHUNK_SHIFT_Z - SUBVOXEL_BITS));
    }

    /** @brief ChunkId for the chunk that contains sub-voxel coordinates (sx, sy, sz).
     *  Uses arithmetic right-shift for correct negative coordinate handling.
     *  Matches client-side naming: chunkIdFromSubVoxelPos()
     */
    static constexpr ChunkId fromSubVoxelPos(SubVoxelCoord subX, SubVoxelCoord subY, SubVoxelCoord subZ) noexcept {
        return make(
            subY >> CHUNK_SHIFT_Y,
            subX >> CHUNK_SHIFT_X,
            subZ >> CHUNK_SHIFT_Z);
    }

    /** @brief Y component, signed 8-bit (range [-128, 127]). */
    constexpr ChunkCoord y() const noexcept {
        int32_t v = static_cast<int32_t>((packed >> 56) & 0xFF);
        return (v & 0x80) ? (v | 0xFFFFFF00) : v;
    }

    /** @brief X component, signed 28-bit. */
    constexpr ChunkCoord x() const noexcept {
        int32_t v = static_cast<int32_t>((packed >> 28) & 0xFFFFFFF);
        return (v & 0x8000000) ? (v | static_cast<int32_t>(0xF0000000)) : v;
    }

    /** @brief Z component, signed 28-bit. */
    constexpr ChunkCoord z() const noexcept {
        int32_t v = static_cast<int32_t>(packed & 0xFFFFFFF);
        return (v & 0x8000000) ? (v | static_cast<int32_t>(0xF0000000)) : v;
    }

    constexpr bool operator==(const ChunkId& o) const noexcept { return packed == o.packed; }
    constexpr bool operator!=(const ChunkId& o) const noexcept { return packed != o.packed; }
    constexpr bool operator< (const ChunkId& o) const noexcept { return packed <  o.packed; }
};

/** @brief Stream output for ChunkId (outputs packed value and unpacked coordinates). */
inline std::ostream& operator<<(std::ostream& os, const ChunkId& id) {
    return os << "ChunkId(" << id.packed << ": " << id.x() << ", " << id.y() << ", " << id.z() << ")";
}

/** @brief Packed voxel index: uint5(y) | uint5(x) | uint5(z) into 15 bits.
 *         Used for delta encoding (wire format) and direct array indexing.
 *         Bit layout: [14:10] y (5-bit) | [9:5] x (5-bit) | [4:0] z (5-bit) */
using VoxelIndex = uint16_t;

/** @brief Create a VoxelIndex from its three unsigned voxel coordinates (0-31).
 *  Matches client-side naming: voxelIndexFromPos()
 */
inline constexpr VoxelIndex voxelIndexFromPos(uint32_t voxelX, uint32_t voxelY, uint32_t voxelZ) noexcept {
    return static_cast<VoxelIndex>(((voxelY & 0x1F) << 10) | ((voxelX & 0x1F) << 5) | (voxelZ & 0x1F));
}

/** @brief Voxel coordinates unpacked from VoxelIndex. */
struct VoxelIndexPos { uint32_t x, y, z; };

/** @brief Extract voxel coordinates from a packed VoxelIndex.
 *  Matches client-side naming: getVoxelIndexPos()
 */
inline constexpr VoxelIndexPos getVoxelIndexPos(VoxelIndex idx) noexcept {
    return VoxelIndexPos{ 
        static_cast<uint32_t>((idx >> 5) & 0x1F), 
        static_cast<uint32_t>((idx >> 10) & 0x1F), 
        static_cast<uint32_t>(idx & 0x1F) 
    };
}

/** @brief Voxel type byte. */
using VoxelType = uint8_t;

/** @brief Global entity identifier (uint32, stable across chunk moves and server lifetime).
 *
 * Assigned once at entity spawn by GameEngine; never changes during the entity's lifetime.
 * Persisted in GlobalEntityIdComponent; serialized as uint32 on the wire.
 */
using GlobalEntityId = uint32_t;

/** @brief Persistent player identifier (uint64).
 * 
 * Derived deterministically from the first 8 bytes of the 16-byte session token.
 * This allows stateless player identification across reconnections.
 */
using PlayerId = uint64_t;

/** @brief Gateway instance identifier (uint32). */
using GatewayId = uint32_t;

/** @brief Chunk coordinates unpacked from ChunkId. */
struct ChunkPos { ChunkCoord x, y, z; };

/**
 * @brief Extract chunk coordinates from a packed ChunkId.
 * Matches client-side naming: getChunkPos()
 */
inline constexpr ChunkPos getChunkPos(ChunkId chunkId) noexcept {
    return ChunkPos{ chunkId.x(), chunkId.y(), chunkId.z() };
}

/**
 * @brief Convert sub-voxel position to chunk coordinates.
 * @param sx,sy,sz Position in sub-voxels.
 * @return ChunkPos containing chunk coordinates.
 */
inline constexpr ChunkPos subVoxelToChunkPos(SubVoxelCoord sx, SubVoxelCoord sy, SubVoxelCoord sz) noexcept {
    return ChunkPos{
        sx >> CHUNK_SHIFT_X,
        sy >> CHUNK_SHIFT_Y,
        sz >> CHUNK_SHIFT_Z
    };
}

/** @brief Minimum payload size (bytes) that triggers LZ4 compression. */
inline constexpr size_t LZ4_COMPRESSION_THRESHOLD = 256;

// ── Physics constants ─────────────────────────────────────────────────────
inline constexpr int32_t TICK_RATE = 20;                              ///< Ticks per second.
inline constexpr float   TICK_DT   = 1.0f / static_cast<float>(TICK_RATE); ///< Seconds per tick (kept for non-physics callers).
inline constexpr float   GRAVITY   = 9.81f;                           ///< Gravitational acceleration (m/s², reference).
/// Gravity applied to vy each tick (sub-vox/tick²) = round(9.81 × TICK_DT² × SUBVOXEL_SIZE).
inline constexpr int32_t GRAVITY_DECREMENT  = 6;
/// Terminal velocity in sub-voxels/tick (≈ 128 m/s at 20 TPS).
inline constexpr int32_t TERMINAL_VELOCITY  = 128 * SUBVOXEL_SIZE / TICK_RATE;
/// Player AABB half-extents in sub-voxels (0.4 × 0.9 × 0.4 voxels).
inline constexpr int32_t PLAYER_BBOX_HX     = static_cast<int32_t>(0.4f * SUBVOXEL_SIZE);
inline constexpr int32_t PLAYER_BBOX_HY     = static_cast<int32_t>(0.9f * SUBVOXEL_SIZE);
inline constexpr int32_t PLAYER_BBOX_HZ     = static_cast<int32_t>(0.4f * SUBVOXEL_SIZE);

// ── Input-system movement speeds (sub-voxels per tick) ────────────────────
// These constants now live in game/entities/GhostPlayerEntity.hpp and PlayerEntity.hpp

} // namespace voxelmmo

// ── std::hash support for use in unordered containers ──────────────────────
namespace std {
template<> struct hash<voxelmmo::ChunkId> {
    size_t operator()(const voxelmmo::ChunkId& id) const noexcept {
        return std::hash<int64_t>{}(id.packed);
    }
};
} // namespace std
