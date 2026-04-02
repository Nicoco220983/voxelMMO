// @ts-check

/**
 * @typedef {Object} VoxelDef
 * @property {number} type - VoxelType value
 * @property {string} name - Human-readable name
 * @property {string | Record<number|string, string>} textures
 *   - If string, used for all 6 faces.
 *   - If object, keys are face indices (0-5) or "default" fallback.
 *   Face order: +X(0), -X(1), +Y(2), -Y(3), +Z(4), -Z(5).
 */

import { BasicVoxel } from './BasicVoxel.js'
import { StoneVoxel } from './StoneVoxel.js'
import { DirtVoxel } from './DirtVoxel.js'
import { PlanksVoxel } from './PlanksVoxel.js'
import { BricksVoxel } from './BricksVoxel.js'

export { BasicVoxel, StoneVoxel, DirtVoxel, PlanksVoxel, BricksVoxel }

/** @type {VoxelDef[]} */
export const VOXEL_DEFS = [
  StoneVoxel,
  DirtVoxel,
  BasicVoxel,
  PlanksVoxel,
  BricksVoxel,
]

/** @type {Map<number, VoxelDef>} */
const defByType = new Map(VOXEL_DEFS.map(d => [d.type, d]))

/**
 * Get voxel definition by type.
 * @param {number} vtype
 * @returns {VoxelDef|undefined}
 */
export function getVoxelDef(vtype) {
  return defByType.get(vtype)
}

/**
 * Get the main texture name for a voxel type (for icons).
 * Returns the default texture or the string texture value.
 * @param {number} vtype
 * @returns {string}
 */
export function getVoxelMainTexture(vtype) {
  const def = defByType.get(vtype)
  if (!def) return ''
  if (typeof def.textures === 'string') {
    return def.textures
  }
  return def.textures.default ?? Object.values(def.textures)[0] ?? ''
}
