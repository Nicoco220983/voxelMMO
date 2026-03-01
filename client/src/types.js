// @ts-check
// ── Type definitions via JSDoc ──────────────────────────────────────────────
// Import this file only for its typedefs; it exports no runtime values.

/**
 * Packed ChunkId as a BigInt: sint6(y) | sint29(x) | sint29(z) in 64 bits.
 * @typedef {bigint} ChunkIdPacked
 */

/**
 * Packed VoxelId as a number: uint4(y) | uint6(x) | uint6(z) in 16 bits.
 * @typedef {number} VoxelIdPacked
 */

/**
 * Voxel type byte (0 = air).
 * @typedef {number} VoxelType
 */

/**
 * Entity identifier (uint16).
 * @typedef {number} EntityId
 */

/**
 * Player identifier (uint32).
 * @typedef {number} PlayerId
 */

// ── ChunkMessageType ─────────────────────────────────────────────────────────

/**
 * First byte of every incoming binary chunk-state message.
 * Odd values are the LZ4-compressed counterpart of the preceding even value.
 * @readonly
 * @enum {number}
 */
export const ChunkMessageType = Object.freeze({
  SNAPSHOT:                  0,
  SNAPSHOT_COMPRESSED:       1,
  SNAPSHOT_DELTA:            2,
  SNAPSHOT_DELTA_COMPRESSED: 3,
  TICK_DELTA:                4,
  TICK_DELTA_COMPRESSED:     5,
})

/**
 * Sub-type of each entity record inside a delta message.
 * @readonly
 * @enum {number}
 */
export const DeltaType = Object.freeze({
  NEW_ENTITY:    0,
  UPDATE_ENTITY: 1,
  DELETE_ENTITY: 2,
})

/**
 * Known entity types (must stay in sync with server MessageTypes.hpp).
 * @readonly
 * @enum {number}
 */
export const EntityType = Object.freeze({
  PLAYER: 0,
})

// ── Chunk dimensions (must stay in sync with server Types.hpp) ───────────────

/** @type {number} */ export const CHUNK_SIZE_Y      = 16
/** @type {number} */ export const CHUNK_SIZE_X      = 64
/** @type {number} */ export const CHUNK_SIZE_Z      = 64
/** @type {number} */ export const CHUNK_VOXEL_COUNT = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z

// ── VoxelType (must stay in sync with server common/VoxelTypes.hpp) ─────────

/**
 * Known voxel type values (uint8). 0 = air, never rendered.
 * @readonly
 * @enum {number}
 */
export const VoxelType = Object.freeze({
  AIR:   0,
  STONE: 1,
  DIRT:  2,
  GRASS: 3,
})

// ── Sub-voxel constants (must match server Types.hpp) ────────────────────────

/** @type {number} */ export const SUBVOXEL_BITS = 8
/** @type {number} */ export const SUBVOXEL_SIZE = 1 << SUBVOXEL_BITS   // 256

// ── Physics constants (must match server Types.hpp) ──────────────────────────

/** @type {number} */ export const TICK_RATE         = 20      // ticks per second
/** @type {number} */ export const GRAVITY           = 9.81    // m/s² (reference)
/** @type {number} */ export const GRAVITY_DECREMENT = 6       // sub-voxels/tick² (mirrors server)

// ── Component dirty-bit constants (must match server DynamicPositionComponent.hpp) ──

/** @type {number} */ export const POSITION_BIT = 1 << 0
