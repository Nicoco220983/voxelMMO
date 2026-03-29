// @ts-check
import * as THREE from 'three'
import { VoxelType } from './types.js'

/** @type {Record<number, string>} */
const VOXEL_TEXTURE_PATHS = {
  [VoxelType.STONE]: '/assets/voxels/stone.png',
  [VoxelType.DIRT]:  '/assets/voxels/dirt.png',
  [VoxelType.GRASS]: '/assets/voxels/grass.png',
}

/** Number of atlas tiles horizontally. */
const ATLAS_TILE_COUNT = Object.keys(VOXEL_TEXTURE_PATHS).length || 1

/**
 * Build a canvas-based atlas from loaded images.
 * @param {HTMLImageElement[]} images
 * @returns {HTMLCanvasElement}
 */
function buildAtlasCanvas(images) {
  const size = 64  // per-tile resolution (source images are expected to be <= this)
  const canvas = document.createElement('canvas')
  canvas.width  = size * ATLAS_TILE_COUNT
  canvas.height = size
  const ctx = canvas.getContext('2d')
  if (!ctx) throw new Error('Failed to get 2D context for atlas')

  images.forEach((img, i) => {
    ctx.drawImage(img, i * size, 0, size, size)
  })

  return canvas
}

/**
 * @typedef {Object} VoxelTextureAtlas
 * @property {THREE.CanvasTexture} texture
 * @property {boolean} loaded
 * @property {() => void} [onLoad]
 */

/** Shared atlas texture. Starts as a 1x1 white placeholder. */
function createPlaceholderCanvas() {
  if (typeof document !== 'undefined') {
    const canvas = document.createElement('canvas')
    canvas.width  = 1
    canvas.height = 1
    const ctx = canvas.getContext('2d')
    if (ctx) {
      ctx.fillStyle = '#ffffff'
      ctx.fillRect(0, 0, 1, 1)
    }
    return canvas
  }
  // Minimal fallback for Node/test environments
  return { width: 1, height: 1 }
}

export const voxelTextureAtlas = new THREE.CanvasTexture(createPlaceholderCanvas())
voxelTextureAtlas.magFilter = THREE.NearestFilter
voxelTextureAtlas.minFilter = THREE.NearestFilter
voxelTextureAtlas.colorSpace = THREE.SRGBColorSpace

/** @type {boolean} */
export let voxelTexturesLoaded = false

/** @type {Array<() => void>} */
const loadCallbacks = []

/**
 * Register a callback to fire once (or immediately if already loaded).
 * @param {() => void} cb
 */
export function onVoxelTexturesLoaded(cb) {
  if (voxelTexturesLoaded) {
    cb()
  } else {
    loadCallbacks.push(cb)
  }
}

/**
 * Get UV coordinates for a voxel type in the atlas.
 * @param {number} vtype
 * @returns {{u0: number, v0: number, u1: number, v1: number}}
 */
export function getVoxelUvs(vtype) {
  const types = Object.keys(VOXEL_TEXTURE_PATHS).map(Number)
  const idx = types.indexOf(vtype)
  if (idx < 0) {
    // Unknown type: return full atlas (will show white placeholder or first tile)
    return { u0: 0, v0: 0, u1: 1, v1: 1 }
  }
  const u0 = idx / ATLAS_TILE_COUNT
  const u1 = (idx + 1) / ATLAS_TILE_COUNT
  return { u0, v0: 0, u1, v1: 1 }
}

// ── Async loading ───────────────────────────────────────────────────────────

const loader = new THREE.TextureLoader()

/** @type {Promise<void>} */
export const voxelTexturesReady =
  typeof document === 'undefined'
    ? Promise.resolve()
    : Promise.all(
        Object.entries(VOXEL_TEXTURE_PATHS).map(([vtype, path]) =>
          new Promise((resolve, reject) => {
            loader.load(
              path,
              (texture) => {
                const img = texture.image
                resolve({ vtype: Number(vtype), img })
              },
              undefined,
              (err) => reject(err)
            )
          })
        )
      ).then((results) => {
        /** @type {Array<{vtype: number, img: HTMLImageElement}>} */
        const typed = /** @type {any} */ (results)
        // Sort by vtype to keep atlas order deterministic
        typed.sort((a, b) => a.vtype - b.vtype)
        const images = typed.map(r => r.img)

        const canvas = buildAtlasCanvas(images)
        voxelTextureAtlas.image = canvas
        voxelTextureAtlas.needsUpdate = true
        voxelTexturesLoaded = true

        loadCallbacks.forEach(cb => cb())
        loadCallbacks.length = 0
      }).catch((err) => {
        console.error('[VoxelTextures] Failed to load voxel textures:', err)
      })
