// @ts-check

/**
 * @typedef {Object} RenderContext
 * @property {number} x - Voxel X position (local chunk coords)
 * @property {number} y - Voxel Y position
 * @property {number} z - Voxel Z position
 * @property {number} vtype - Voxel type value
 * @property {number} jitter - Brightness jitter for this voxel
 * @property {number[]} positions - Vertex positions array to push to
 * @property {number[]} colors - Vertex colors array to push to
 * @property {number[]} uvs - UV coordinates array to push to
 * @property {number[]} normals - Normals array to push to
 * @property {number[]} indices - Triangle indices array to push to
 * @property {function(number,number,number): boolean} isSolid - Check if neighbor is solid
 * @property {function(number,number): {u0:number,v0:number,u1:number,v1:number}} getVoxelUvs - Get UVs
 * @property {Array<[number,number,number]>} FACE_DIRS - Face direction offsets
 * @property {number[]} FACE_SHADE - Face shading multipliers
 * @property {Array<[number,number,number]>} FACE_NORMALS - Face normals
 * @property {Array<Array<[number,number,number]>>} FACE_VERTS - Face vertex positions
 */

/**
 * @typedef {Object} VoxelDef
 * @property {number} type - VoxelType value
 * @property {string} name - Human-readable name
 * @property {string | Record<number|string, string>} textures
 *   - If string, used for all 6 faces.
 *   - If object, keys are face indices (0-5) or "default" fallback.
 *   Face order: +X(0), -X(1), +Y(2), -Y(3), +Z(4), -Z(5).
 * @property {boolean} [isSolid=true] - Whether this voxel is solid (blocks movement, can attach to).
 *   Non-solid voxels (like ladders) render differently.
 * @property {boolean} [isOpaque=true] - Whether this voxel is opaque (completely fills the block volume).
 *   Used for face culling: opaque neighbors hide shared faces; transparent neighbors don't.
 * @property {function(RenderContext): void} [renderCustom] - Custom render function (if provided, used instead of default cube faces).
 */

import { BaseVoxel, createVoxel } from './BaseVoxel.js'
import { BasicVoxel } from './BasicVoxel.js'
import { StoneVoxel } from './StoneVoxel.js'
import { DirtVoxel } from './DirtVoxel.js'
import { PlanksVoxel } from './PlanksVoxel.js'
import { BricksVoxel } from './BricksVoxel.js'
import { SlimeVoxel } from './SlimeVoxel.js'
import { MudVoxel } from './MudVoxel.js'
import { LadderVoxel } from './LadderVoxel.js'

export { BaseVoxel, createVoxel }
export { BasicVoxel, StoneVoxel, DirtVoxel, PlanksVoxel, BricksVoxel, SlimeVoxel, MudVoxel, LadderVoxel }

/** @type {VoxelDef[]} */
export const VOXEL_DEFS = [
  StoneVoxel,
  DirtVoxel,
  BasicVoxel,
  PlanksVoxel,
  BricksVoxel,
  SlimeVoxel,
  MudVoxel,
  LadderVoxel,
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
