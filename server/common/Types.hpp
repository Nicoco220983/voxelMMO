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
    static constexpr ChunkId make(int8_t y, int32_t x, int32_t z) noexcept {
        ChunkId id;
        id.packed = (static_cast<int64_t>(y  & 0x3F)        << 58)
                  | (static_cast<int64_t>(x  & 0x1FFFFFFF)  << 29)
                  |  static_cast<int64_t>(z  & 0x1FFFFFFF);
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

/**
 * @brief Voxel identifier packed as uint4(y) | uint6(x) | uint6(z) into 16 bits.
 *
 * Bit layout: [15:12] y (4-bit) | [11:6] x (6-bit) | [5:0] z (6-bit)
 *
 * Because chunk dimensions are exactly 16×64×64, the packed value equals
 * y*4096 + x*64 + z, which is the flat (y,x,z) row-major index into
 * WorldChunk::voxels[].  Use vid.packed directly to index the array.
 */
struct VoxelId {
    uint16_t packed{0};

    /** @brief Construct a VoxelId from its three unsigned components. */
    static constexpr VoxelId make(uint8_t y, uint8_t x, uint8_t z) noexcept {
        VoxelId id;
        id.packed = static_cast<uint16_t>(
            ((y & 0x0F) << 12) | ((x & 0x3F) << 6) | (z & 0x3F));
        return id;
    }

    /** @brief Y component, 4-bit (range [0, 15]). */
    constexpr uint8_t y() const noexcept { return (packed >> 12) & 0x0F; }
    /** @brief X component, 6-bit (range [0, 63]). */
    constexpr uint8_t x() const noexcept { return (packed >>  6) & 0x3F; }
    /** @brief Z component, 6-bit (range [0, 63]). */
    constexpr uint8_t z() const noexcept { return  packed        & 0x3F; }

    constexpr bool operator==(const VoxelId& o) const noexcept { return packed == o.packed; }
};

/** @brief Voxel type byte. */
using VoxelType = uint8_t;

/** @brief Per-chunk wire entity id (uint16, unique within one chunk's lifetime). */
using ChunkEntityId = uint16_t;

/** @brief Persistent player identifier (uint32). */
using PlayerId = uint32_t;

/** @brief Gateway instance identifier (uint32). */
using GatewayId = uint32_t;

// ── Chunk voxel dimensions (must match VoxelId bit widths) ──────────────────
inline constexpr uint8_t CHUNK_SIZE_Y = 16;   ///< uint4 range [0,15]
inline constexpr uint8_t CHUNK_SIZE_X = 64;   ///< uint6 range [0,63]
inline constexpr uint8_t CHUNK_SIZE_Z = 64;   ///< uint6 range [0,63]
inline constexpr size_t  CHUNK_VOXEL_COUNT = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z; // 65 536

/// Sub-voxel precision: 1 voxel = 256 position units.
inline constexpr int     SUBVOXEL_BITS = 8;
inline constexpr int32_t SUBVOXEL_SIZE = 1 << SUBVOXEL_BITS;  ///< 256
inline constexpr int32_t SUBVOXEL_MASK = SUBVOXEL_SIZE - 1;   ///< 0xFF

/// Bit-shift from sub-voxel position to chunk coordinate = log2(chunk_dim × SUBVOXEL_SIZE).
inline constexpr int CHUNK_SHIFT_Y = 12;  ///< log2(CHUNK_SIZE_Y × SUBVOXEL_SIZE) = log2(16 × 256)
inline constexpr int CHUNK_SHIFT_X = 14;  ///< log2(CHUNK_SIZE_X × SUBVOXEL_SIZE) = log2(64 × 256)
inline constexpr int CHUNK_SHIFT_Z = 14;  ///< log2(CHUNK_SIZE_Z × SUBVOXEL_SIZE) = log2(64 × 256)

/// Bitmasks for extracting local voxel coordinates within a chunk.
inline constexpr int CHUNK_MASK_Y = CHUNK_SIZE_Y - 1;  ///< 0x0F
inline constexpr int CHUNK_MASK_X = CHUNK_SIZE_X - 1;  ///< 0x3F
inline constexpr int CHUNK_MASK_Z = CHUNK_SIZE_Z - 1;  ///< 0x3F

/**
 * @brief ChunkId for the chunk that contains integer world-voxel coordinate (ix, iy, iz).
 *
 * Uses arithmetic right-shift (guaranteed by C++20 two's complement) instead of
 * division so negative coordinates floor correctly without a branch.
 */
inline constexpr ChunkId chunkIdOf(int32_t ix, int32_t iy, int32_t iz) noexcept {
    return ChunkId::make(
        static_cast<int8_t>(iy >> CHUNK_SHIFT_Y),
        ix >> CHUNK_SHIFT_X,
        iz >> CHUNK_SHIFT_Z);
}

/** @brief Minimum payload size (bytes) that triggers LZ4 compression. */
inline constexpr size_t LZ4_COMPRESSION_THRESHOLD = 256;

// ── Physics constants ─────────────────────────────────────────────────────
inline constexpr int32_t TICK_RATE = 20;                              ///< Ticks per second.
inline constexpr float   TICK_DT   = 1.0f / static_cast<float>(TICK_RATE); ///< Seconds per tick (kept for non-physics callers).
inline constexpr float   GRAVITY   = 9.81f;                           ///< Gravitational acceleration (m/s², reference).
/// Gravity applied to vy each tick (sub-vox/tick²) = round(9.81 × TICK_DT² × SUBVOXEL_SIZE).
inline constexpr int32_t GRAVITY_DECREMENT = 6;

} // namespace voxelmmo

// ── std::hash support for use in unordered containers ──────────────────────
namespace std {
template<> struct hash<voxelmmo::ChunkId> {
    size_t operator()(const voxelmmo::ChunkId& id) const noexcept {
        return std::hash<int64_t>{}(id.packed);
    }
};
} // namespace std
