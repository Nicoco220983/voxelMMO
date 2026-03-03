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
 * Per-chunk wire entity id (uint16, unique within one chunk's lifetime).
 * @typedef {number} ChunkEntityId
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
  SELF_ENTITY:               6,  // type(1)+ChunkId(8)+tick(4)+ChunkEntityId(2) = 15 bytes
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
  PLAYER:       0,  // Full-physics player (gravity + collision)
  GHOST_PLAYER: 1,  // Ghost player (noclip, no gravity)
})

/**
 * First byte of every client → server binary WebSocket frame.
 * @readonly
 * @enum {number}
 */
export const ClientMessageType = Object.freeze({
  INPUT: 0,  // uint8 buttons | float32 yaw | float32 pitch — 9-byte payload (total 10 bytes)
  JOIN:  1,  // uint8 EntityType — 1-byte payload (total 2 bytes)
})

/**
 * Bitmask flags for the INPUT message buttons field (must stay in sync with server InputButton).
 * @readonly
 * @enum {number}
 */
export const InputButton = Object.freeze({
  FORWARD:  1 << 0,
  BACKWARD: 1 << 1,
  LEFT:     1 << 2,
  RIGHT:    1 << 3,
  JUMP:     1 << 4,
  DESCEND:  1 << 5,
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

// ── Input-system speed mirrors (must match server Types.hpp) ─────────────────

/** @type {number} */ export const GHOST_MOVE_SPEED_VOXELS  = 20.0  // voxels/s (server: GHOST_MOVE_SPEED=256 sub-vox/tick)
/** @type {number} */ export const PLAYER_WALK_SPEED_VOXELS = 6.0   // voxels/s (server: PLAYER_WALK_SPEED=77 sub-vox/tick)
/** @type {number} */ export const PLAYER_JUMP_VY_VOXELS    = 110 / 256 * TICK_RATE  // ≈ 8.6 voxels/s initial vy

// ── Component dirty-bit constants (must match server DynamicPositionComponent.hpp) ──

/** @type {number} */ export const POSITION_BIT = 1 << 0
