// @ts-check
// ── Type definitions via JSDoc ──────────────────────────────────────────────
// Import this file only for its typedefs; it exports no runtime values.

/**
 * Packed ChunkId as a BigInt: sint6(y) | sint29(x) | sint29(z) in 64 bits.
 * @typedef {bigint} ChunkIdPacked
 */

/**
 * Packed VoxelId as a number: uint5(y) | uint5(x) | uint5(z) in 15 bits.
 * @typedef {number} VoxelIdPacked
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
 * Player identifier (uint32).
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
/** @type {number} */ export const SHEEP_BEHAVIOR_BIT = 1 << 1

// ── Entity lifecycle flags (must match server DirtyComponent.hpp) ──

/** @type {number} Entity newly created (bit 6) */ export const CREATED_BIT = 1 << 6
/** @type {number} Entity deleted (bit 7, reserved for future use) */ export const DELETED_BIT = 1 << 7
