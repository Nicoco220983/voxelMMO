// @ts-check
// ── Type definitions via JSDoc ──────────────────────────────────────────────
// Import this file for its typedefs and ChunkId helper functions.

/**
 * Sub-voxel coordinate (1 voxel = 256 sub-voxel units).
 * @typedef {number} SubVoxelCoord
 */

/**
 * Voxel coordinate (world-space in voxels).
 * @typedef {number} VoxelCoord
 */

/**
 * Chunk coordinate.
 * @typedef {number} ChunkCoord
 */

/**
 * Packed ChunkId as a BigInt: sint8(y) | sint28(x) | sint28(z) in 64 bits.
 * @typedef {bigint} ChunkId
 */

/**
 * Packed VoxelIndex as a number: uint5(y) | uint5(x) | uint5(z) in 15 bits.
 * Used as flat array index into the voxels[] buffer.
 * @typedef {number} VoxelIndex
 */

/**
 * Voxel type byte (0 = air).
 * @typedef {number} VoxelType
 */

/**
 * Global entity identifier (uint32, stable across chunk moves and server lifetime).
 * @typedef {number} GlobalEntityId
 */

/**
 * Player identifier (uint64).
 * Derived deterministically from session token (first 8 bytes).
 * Note: JavaScript numbers can precisely represent uint64 up to 2^53-1,
 * but we use bigint for full uint64 range in future if needed.
 * @typedef {number} PlayerId
 */

// ── Chunk dimensions (must stay in sync with server Types.hpp) ───────────────

/** @type {number} */ export const CHUNK_SIZE_Y      = 32
/** @type {number} */ export const CHUNK_SIZE_X      = 32
/** @type {number} */ export const CHUNK_SIZE_Z      = 32
/** @type {number} */ export const CHUNK_VOXEL_COUNT = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z

// ── VoxelType (must stay in sync with server common/VoxelTypes.hpp) ─────────
// NOTE: VoxelType enum moved to VoxelTypes.js for cleaner structure (mirrors server).
// Import from there: import { VoxelType } from './VoxelTypes.js'

// ── Sub-voxel constants (must match server Types.hpp) ────────────────────────

/** @type {number} */ export const SUBVOXEL_BITS = 8
/** @type {number} */ export const SUBVOXEL_SIZE = 1 << SUBVOXEL_BITS   // 256

// ── Chunk shift constants (must match server Types.hpp) ──────────────────────
// Bit-shift from sub-voxel position to chunk coordinate = log2(chunk_dim × SUBVOXEL_SIZE)
/** @type {number} */ export const CHUNK_SHIFT_Y = 13  // log2(32 × 256)
/** @type {number} */ export const CHUNK_SHIFT_X = 13  // log2(32 × 256)
/** @type {number} */ export const CHUNK_SHIFT_Z = 13  // log2(32 × 256)

// ── ChunkId helper functions ─────────────────────────────────────────────────

const MASK_8 = 0xFFn
const MASK_28 = 0xFFFFFFFn
const SIGN_BIT_8 = 0x80n
const SIGN_BIT_28 = 0x8000000n
const SIGN_EXT_8 = 0xFFFFFFFFFFFFFF00n
const SIGN_EXT_28 = 0xFFFFFFFFF0000000n

/**
 * Create a ChunkId from its three signed chunk coordinates.
 * @param {ChunkCoord} cx - Chunk X, signed 28-bit
 * @param {ChunkCoord} cy - Chunk Y, signed 8-bit (range [-128, 127])
 * @param {ChunkCoord} cz - Chunk Z, signed 28-bit
 * @returns {ChunkId}
 */
export function chunkIdFromChunkPos(cx, cy, cz) {
  const packed = (BigInt(BigInt.asIntN(32, BigInt(cy)) & MASK_8) << 56n)
               | (BigInt(BigInt.asIntN(32, BigInt(cx)) & MASK_28) << 28n)
               | BigInt(BigInt.asIntN(32, BigInt(cz)) & MASK_28)
  return BigInt.asIntN(64, packed)
}

/**
 * Compute ChunkId for the chunk containing world voxel coordinates (vx, vy, vz).
 * Uses floor division for correct negative coordinate handling.
 * Note: cannot use bitwise shift (>>) because it truncates to 32-bit, causing
 * incorrect results for coordinates beyond 32-bit signed int range (~262k voxels).
 * Divides by CHUNK_SIZE (32), not by (1 << CHUNK_SHIFT) which is for sub-voxels.
 * @param {VoxelCoord} vx - World voxel X
 * @param {VoxelCoord} vy - World voxel Y
 * @param {VoxelCoord} vz - World voxel Z
 * @returns {ChunkId}
 */
export function chunkIdFromVoxelPos(vx, vy, vz) {
  return chunkIdFromChunkPos(
    Math.floor(vx / CHUNK_SIZE_X),
    Math.floor(vy / CHUNK_SIZE_Y),
    Math.floor(vz / CHUNK_SIZE_Z)
  )
}

/**
 * Compute ChunkId for the chunk containing sub-voxel coordinates.
 * Uses floor division for correct negative coordinate handling.
 * Note: cannot use bitwise shift (>>) because it truncates to 32-bit.
 * @param {SubVoxelCoord} sx - Sub-voxel X coordinate
 * @param {SubVoxelCoord} sy - Sub-voxel Y coordinate
 * @param {SubVoxelCoord} sz - Sub-voxel Z coordinate
 * @returns {ChunkId}
 */
export function chunkIdFromSubVoxelPos(sx, sy, sz) {
  return chunkIdFromChunkPos(
    Math.floor(sx / (1 << CHUNK_SHIFT_X)),
    Math.floor(sy / (1 << CHUNK_SHIFT_Y)),
    Math.floor(sz / (1 << CHUNK_SHIFT_Z))
  )
}

/**
 * Extract chunk coordinates from a packed ChunkId.
 * @param {ChunkId} chunkId
 * @returns {{cx: ChunkCoord, cy: ChunkCoord, cz: ChunkCoord}}
 */
export function getChunkPos(chunkId) {
  const packed = BigInt.asIntN(64, chunkId)
  
  // Extract Y (8 bits at bit 56) - use shift trick for sign extension
  const yRaw = Number((packed >> 56n) & MASK_8)
  const cy = (yRaw << 24) >> 24  // sign extend from 8 bits to 32 bits
  
  // Extract X (28 bits at bit 28) - use shift trick for sign extension
  const xRaw = Number((packed >> 28n) & MASK_28)
  const cx = (xRaw << 4) >> 4    // sign extend from 28 bits to 32 bits
  
  // Extract Z (28 bits at bit 0) - use shift trick for sign extension
  const zRaw = Number(packed & MASK_28)
  const cz = (zRaw << 4) >> 4    // sign extend from 28 bits to 32 bits
  
  return { cx, cy, cz }
}

/**
 * String representation of a ChunkId for debugging.
 * @param {ChunkId} chunkId
 * @returns {string}
 */
export function chunkIdToString(chunkId) {
  const { cx, cy, cz } = getChunkPos(chunkId)
  return `ChunkId(${cx}, ${cy}, ${cz})`
}

// ── VoxelIndex helper functions ──────────────────────────────────────────────

/**
 * Create a VoxelIndex from its three unsigned voxel coordinates (0-31).
 * Layout: uint5(y) | uint5(x) | uint5(z) in 15 bits.
 * @param {number} vx - Local voxel X, 0-31
 * @param {number} vy - Local voxel Y, 0-31
 * @param {number} vz - Local voxel Z, 0-31
 * @returns {VoxelIndex}
 */
export function voxelIndexFromPos(vx, vy, vz) {
  return ((vy & 0x1F) << 10) | ((vx & 0x1F) << 5) | (vz & 0x1F)
}

/**
 * Extract voxel coordinates from a packed VoxelIndex.
 * @param {VoxelIndex} voxelIndex
 * @returns {{vx: number, vy: number, vz: number}}
 */
export function getVoxelIndexPos(voxelIndex) {
  return {
    vy: (voxelIndex >> 10) & 0x1F,
    vx: (voxelIndex >> 5) & 0x1F,
    vz: voxelIndex & 0x1F,
  }
}

/**
 * String representation of a VoxelIndex for debugging.
 * @param {VoxelIndex} voxelIndex
 * @returns {string}
 */
export function voxelIndexToString(voxelIndex) {
  const { vx, vy, vz } = getVoxelIndexPos(voxelIndex)
  return `VoxelIndex(${vx}, ${vy}, ${vz})`
}

// ── Physics constants (must match server Types.hpp) ──────────────────────────

/** @type {number} */ export const TICK_RATE         = 20      // ticks per second
/** @type {number} */ export const GRAVITY           = 9.81    // m/s² (reference)
/** @type {number} */ export const GRAVITY_DECREMENT = 6       // sub-voxels/tick² (mirrors server)

// Note: Entity lifecycle is tracked via DeltaType (CREATE_ENTITY, UPDATE_ENTITY, DELETE_ENTITY, CHUNK_CHANGE_ENTITY)
// in the message itself, not via component flags. See NetworkProtocol.js for DeltaType enum.
// Component dirty-bit constants are defined in components/ComponentBits.js
