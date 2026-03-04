#pragma once
#include <cstdint>
#include <functional>

namespace voxelmmo {

/**
 * @brief Chunk identifier packed as sint6(y) | sint29(x) | sint29(z) into 64 bits.
 *
 * Bit layout (MSB first): [63:58] y (6-bit signed) | [57:29] x (29-bit signed) | [28:0] z (29-bit signed)
 *
 * World extents: y ∈ [-32, 31], x/z ∈ [-268 435 456, 268 435 455]
 */
struct ChunkId {
    int64_t packed{0};

    /** @brief Construct a ChunkId from its three signed components. */
    static constexpr ChunkId make(int8_t chunkY, int32_t chunkX, int32_t chunkZ) noexcept {
        ChunkId id;
        id.packed = (static_cast<int64_t>(chunkY  & 0x3F)        << 58)
                  | (static_cast<int64_t>(chunkX  & 0x1FFFFFFF)  << 29)
                  |  static_cast<int64_t>(chunkZ  & 0x1FFFFFFF);
        return id;
    }

    /** @brief Y component, signed 6-bit (range [-32, 31]). */
    constexpr int8_t y() const noexcept {
        int8_t v = static_cast<int8_t>((packed >> 58) & 0x3F);
        return (v & 0x20) ? static_cast<int8_t>(v | static_cast<int8_t>(0xC0)) : v;
    }

    /** @brief X component, signed 29-bit. */
    constexpr int32_t x() const noexcept {
        int32_t v = static_cast<int32_t>((packed >> 29) & 0x1FFFFFFF);
        return (v & 0x10000000) ? (v | static_cast<int32_t>(0xE0000000)) : v;
    }

    /** @brief Z component, signed 29-bit. */
    constexpr int32_t z() const noexcept {
        int32_t v = static_cast<int32_t>(packed & 0x1FFFFFFF);
        return (v & 0x10000000) ? (v | static_cast<int32_t>(0xE0000000)) : v;
    }

    constexpr bool operator==(const ChunkId& o) const noexcept { return packed == o.packed; }
    constexpr bool operator!=(const ChunkId& o) const noexcept { return packed != o.packed; }
    constexpr bool operator< (const ChunkId& o) const noexcept { return packed <  o.packed; }
};

/** @brief Packed voxel index: uint5(y) | uint5(x) | uint5(z) into 15 bits.
 *         Used for delta encoding (wire format) and direct array indexing.
 *         Bit layout: [14:10] y (5-bit) | [9:5] x (5-bit) | [4:0] z (5-bit) */
using VoxelIndex = uint16_t;

/** @brief Pack (x,y,z) chunk-local coordinates into a VoxelIndex. */
inline constexpr VoxelIndex packVoxelIndex(uint32_t voxelX, uint32_t voxelY, uint32_t voxelZ) noexcept {
    return static_cast<VoxelIndex>(((voxelY & 0x1F) << 10) | ((voxelX & 0x1F) << 5) | (voxelZ & 0x1F));
}

/** @brief Unpack VoxelIndex into (x,y,z) chunk-local coordinates. */
inline constexpr void unpackVoxelIndex(VoxelIndex idx, uint32_t& x, uint32_t& y, uint32_t& z) noexcept {
    y = (idx >> 10) & 0x1F;
    x = (idx >>  5) & 0x1F;
    z =  idx        & 0x1F;
}

/** @brief Voxel type byte. */
using VoxelType = uint8_t;

/** @brief Per-chunk wire entity id (uint16, unique within one chunk's lifetime). */
using ChunkEntityId = uint16_t;

/** @brief Persistent player identifier (uint32). */
using PlayerId = uint32_t;

/** @brief Gateway instance identifier (uint32). */
using GatewayId = uint32_t;

// ── Chunk voxel dimensions (must match VoxelId bit widths) ──────────────────
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
 * @brief ChunkId for the chunk that contains integer world-voxel coordinate (ix, iy, iz).
 *
 * Uses arithmetic right-shift (guaranteed by C++20 two's complement) instead of
 * division so negative coordinates floor correctly without a branch.
 */
inline constexpr ChunkId chunkIdOf(int32_t worldX, int32_t worldY, int32_t worldZ) noexcept {
    return ChunkId::make(
        static_cast<int8_t>(worldY >> CHUNK_SHIFT_Y),
        worldX >> CHUNK_SHIFT_X,
        worldZ >> CHUNK_SHIFT_Z);
}

/**
 * @brief ChunkId for the chunk that contains voxel coordinates (vx, vy, vz).
 *
 * Voxel coordinates are at a lower resolution than sub-voxel coordinates.
 * Uses arithmetic right-shift for correct negative coordinate handling.
 */
inline constexpr ChunkId chunkIdOfVoxel(int32_t voxelX, int32_t voxelY, int32_t voxelZ) noexcept {
    return ChunkId::make(
        static_cast<int8_t>(voxelY >> (CHUNK_SHIFT_Y - SUBVOXEL_BITS)),
        voxelX >> (CHUNK_SHIFT_X - SUBVOXEL_BITS),
        voxelZ >> (CHUNK_SHIFT_Z - SUBVOXEL_BITS));
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
inline constexpr int32_t GHOST_MOVE_SPEED  = 256;  ///< 20 vox/s × SUBVOXEL_SIZE × TICK_DT
inline constexpr int32_t PLAYER_WALK_SPEED = 77;   ///<  6 vox/s × SUBVOXEL_SIZE × TICK_DT
inline constexpr int32_t PLAYER_JUMP_VY    = 110;  ///< gives ≈ 3.9 voxel jump height

} // namespace voxelmmo

// ── std::hash support for use in unordered containers ──────────────────────
namespace std {
template<> struct hash<voxelmmo::ChunkId> {
    size_t operator()(const voxelmmo::ChunkId& id) const noexcept {
        return std::hash<int64_t>{}(id.packed);
    }
};
} // namespace std
