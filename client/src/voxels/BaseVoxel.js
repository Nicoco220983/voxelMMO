// @ts-check

/**
 * Base class for voxel type definitions.
 * Provides sensible defaults: isSolid=true, isOpaque=true.
 * Most solid voxels (stone, dirt, planks) can use this directly.
 * Transparent or non-solid voxels should set isSolid/isOpaque accordingly.
 */
export class BaseVoxel {
  /** @type {number} */
  type

  /** @type {string} */
  name

  /** @type {string | Record<number|string, string>} */
  textures

  /** @type {boolean} */
  isSolid = true

  /** @type {boolean} */
  isOpaque = true

  /**
   * @param {Object} opts
   * @param {number} opts.type - VoxelType value
   * @param {string} opts.name - Human-readable name
   * @param {string | Record<number|string, string>} opts.textures - Texture mapping
   * @param {boolean} [opts.isSolid=true] - Whether this voxel blocks movement
   * @param {boolean} [opts.isOpaque=true] - Whether this voxel fully occludes faces
   * @param {function(import('./index.js').RenderContext): void} [opts.renderCustom] - Custom render function
   */
  constructor({ type, name, textures, isSolid = true, isOpaque = true, renderCustom }) {
    this.type = type
    this.name = name
    this.textures = textures
    if (isSolid !== true) this.isSolid = isSolid
    if (isOpaque !== true) this.isOpaque = isOpaque
    if (renderCustom) this.renderCustom = renderCustom
  }
}

/**
 * Create a voxel definition with configurable properties.
 * Defaults: isSolid=true, isOpaque=true (typical for solid blocks like stone, dirt).
 * @param {Object} opts
 * @param {number} opts.type - VoxelType value
 * @param {string} opts.name - Human-readable name
 * @param {string | Record<number|string, string>} opts.textures - Texture mapping
 * @param {boolean} [opts.isSolid=true] - Whether this voxel blocks movement
 * @param {boolean} [opts.isOpaque=true] - Whether this voxel fully occludes faces
 * @param {function(import('./index.js').RenderContext): void} [opts.renderCustom] - Custom render function
 * @returns {import('./index.js').VoxelDef}
 */
export function createVoxel({ type, name, textures, isSolid = true, isOpaque = true, renderCustom }) {
  return new BaseVoxel({ type, name, textures, isSolid, isOpaque, renderCustom })
}
