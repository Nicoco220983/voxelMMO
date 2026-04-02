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
 * Packed ChunkId as a BigInt: sint6(y) | sint29(x) | sint29(z) in 64 bits.
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

// ── Game EntityType (must stay in sync with server EntityType.hpp) ───────────

/**
 * Known entity types (must stay in sync with server EntityType.hpp).
 * @readonly
 * @enum {number}
 */
export const EntityType = Object.freeze({
  PLAYER:       0,  // Full-physics player (gravity + collision)
  GHOST_PLAYER: 1,  // Ghost player (noclip, no gravity)
  SHEEP:        2,  // Passive mob: wanders randomly, blocked by voxels
})

/** @type {Record<number, string>} Maps EntityType value to name. */
const ENTITY_TYPE_NAMES = Object.freeze({
  [EntityType.PLAYER]:       'PLAYER',
  [EntityType.GHOST_PLAYER]: 'GHOST_PLAYER',
  [EntityType.SHEEP]:        'SHEEP',
})

/** @type {Record<string, number>} Maps lowercase name to EntityType value. */
const ENTITY_TYPE_BY_NAME = Object.freeze({
  'player':       EntityType.PLAYER,
  'ghost_player': EntityType.GHOST_PLAYER,
  'sheep':        EntityType.SHEEP,
})

/**
 * Convert EntityType enum value to human-readable string name.
 * @param {number} type - The entity type value.
 * @returns {string} The type name (e.g., "PLAYER", "SHEEP") or "UNKNOWN".
 */
export function entityTypeToString(type) {
  return ENTITY_TYPE_NAMES[type] ?? 'UNKNOWN'
}

/**
 * Parse entity type from string name (case-insensitive).
 * @param {string} str - The string to parse (e.g., "sheep", "PLAYER").
 * @returns {number|null} The EntityType value, or null if unrecognized.
 */
export function stringToEntityType(str) {
  return ENTITY_TYPE_BY_NAME[str.toLowerCase()] ?? null
}

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

const MASK_6 = 0x3Fn
const MASK_29 = 0x1FFFFFFFn
const SIGN_BIT_6 = 0x20n
const SIGN_BIT_29 = 0x10000000n
const SIGN_EXT_6 = 0xFFFFFFFFFFFFFFC0n
const SIGN_EXT_29 = 0xFFFFFFFFE0000000n

/**
 * Create a ChunkId from its three signed chunk coordinates.
 * @param {ChunkCoord} cx - Chunk X, signed 29-bit
 * @param {ChunkCoord} cy - Chunk Y, signed 6-bit (range [-32, 31])
 * @param {ChunkCoord} cz - Chunk Z, signed 29-bit
 * @returns {ChunkId}
 */
export function chunkIdFromChunkPos(cx, cy, cz) {
  const packed = (BigInt(BigInt.asIntN(32, BigInt(cy)) & MASK_6) << 58n)
               | (BigInt(BigInt.asIntN(32, BigInt(cx)) & MASK_29) << 29n)
               | BigInt(BigInt.asIntN(32, BigInt(cz)) & MASK_29)
  return BigInt.asIntN(64, packed)
}

/**
 * Compute ChunkId for the chunk containing world voxel coordinates (vx, vy, vz).
 * Uses arithmetic right-shift for correct negative coordinate handling.
 * @param {VoxelCoord} vx - World voxel X
 * @param {VoxelCoord} vy - World voxel Y
 * @param {VoxelCoord} vz - World voxel Z
 * @returns {ChunkId}
 */
export function chunkIdFromVoxelPos(vx, vy, vz) {
  return chunkIdFromChunkPos(
    vx >> CHUNK_SHIFT_X,
    vy >> CHUNK_SHIFT_Y,
    vz >> CHUNK_SHIFT_Z
  )
}

/**
 * Compute ChunkId for the chunk containing sub-voxel coordinates.
 * @param {SubVoxelCoord} sx - Sub-voxel X coordinate
 * @param {SubVoxelCoord} sy - Sub-voxel Y coordinate
 * @param {SubVoxelCoord} sz - Sub-voxel Z coordinate
 * @returns {ChunkId}
 */
export function chunkIdFromSubVoxelPos(sx, sy, sz) {
  return chunkIdFromChunkPos(
    sx >> CHUNK_SHIFT_X,
    sy >> CHUNK_SHIFT_Y,
    sz >> CHUNK_SHIFT_Z
  )
}

/**
 * Extract chunk coordinates from a packed ChunkId.
 * @param {ChunkId} chunkId
 * @returns {{cx: ChunkCoord, cy: ChunkCoord, cz: ChunkCoord}}
 */
export function getChunkPos(chunkId) {
  const packed = BigInt.asIntN(64, chunkId)
  
  // Extract Y (6 bits at bit 58)
  const yRaw = Number((packed >> 58n) & MASK_6)
  const cy = (yRaw & Number(SIGN_BIT_6)) ? (yRaw | Number(SIGN_EXT_6)) : yRaw
  
  // Extract X (29 bits at bit 29)
  const xRaw = Number((packed >> 29n) & MASK_29)
  const cx = (xRaw & Number(SIGN_BIT_29)) ? (xRaw | Number(SIGN_EXT_29)) : xRaw
  
  // Extract Z (29 bits at bit 0)
  const zRaw = Number(packed & MASK_29)
  const cz = (zRaw & Number(SIGN_BIT_29)) ? (zRaw | Number(SIGN_EXT_29)) : zRaw
  
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

// ── Input-system speed mirrors (must match server Types.hpp) ─────────────────

/** @type {number} */ export const GHOST_MOVE_SPEED_VOXELS  = 20.0  // voxels/s (server: GHOST_MOVE_SPEED=256 sub-vox/tick)
/** @type {number} */ export const PLAYER_WALK_SPEED_VOXELS = 6.0   // voxels/s (server: PLAYER_WALK_SPEED=77 sub-vox/tick)
/** @type {number} */ export const PLAYER_JUMP_VY_VOXELS    = 110 / 256 * TICK_RATE  // ≈ 8.6 voxels/s initial vy

// ── Component dirty-bit constants (must match server DynamicPositionComponent.hpp) ──

/** @type {number} */ export const POSITION_BIT = 1 << 0
/** @type {number} */ export const SHEEP_BEHAVIOR_BIT = 1 << 1

// Note: Entity lifecycle is tracked via DeltaType (CREATE_ENTITY, UPDATE_ENTITY, DELETE_ENTITY, CHUNK_CHANGE_ENTITY)
// in the message itself, not via component flags. See NetworkProtocol.js for DeltaType enum.
