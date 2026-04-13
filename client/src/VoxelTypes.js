// @ts-check
/**
 * Voxel type constants (must stay in sync with server/common/VoxelTypes.hpp).
 *
 * @note GRASS has been removed. BASIC is now the first solid voxel type (1).
 */

/**
 * Known voxel type values (uint8). 0 = air, never rendered.
 * @readonly
 * @enum {number}
 */
export const VoxelType = Object.freeze({
  AIR:         0,
  BASIC:       1,
  STONE:       2,
  DIRT:        3,
  PLANKS:      4,
  BRICKS:      5,
  SLIME:       6,  // Bouncy surface
  MUD:         7,  // Slow movement
  LADDER:      8,  // Climbable
  GOBLIN_BED:  9,  // Spawns a goblin when chunk activates
})
