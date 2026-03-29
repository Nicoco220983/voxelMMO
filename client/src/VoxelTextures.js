// @ts-check
import * as THREE from 'three'
import { VOXEL_DEFS } from './voxels/index.js'

/**
 * Collect the unique set of texture names required by all voxel definitions.
 * @returns {string[]}
 */
function collectTextureNames() {
  /** @type {Set<string>} */
  const names = new Set()
  for (const def of VOXEL_DEFS) {
    if (typeof def.textures === 'string') {
      names.add(def.textures)
    } else {
      for (const key of Object.keys(def.textures)) {
        names.add(def.textures[key])
      }
    }
  }
  return Array.from(names).sort()
}

/** Sorted list of unique texture names in the atlas. */
const TEXTURE_NAMES = collectTextureNames()

/** Number of atlas tiles horizontally. */
const ATLAS_TILE_COUNT = Math.max(TEXTURE_NAMES.length, 1)

/**
 * Build a canvas-based atlas from loaded images.
 * @param {HTMLImageElement[]} images
 * @returns {HTMLCanvasElement}
 */
function buildAtlasCanvas(images) {
  const size = 64  // per-tile resolution
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
 * Resolve the texture name for a given voxel type and face.
 * @param {number} vtype
 * @param {number} face
 * @returns {string}
 */
function resolveTextureName(vtype, face) {
  const def = VOXEL_DEFS.find(d => d.type === vtype)
  if (!def) return ''

  if (typeof def.textures === 'string') {
    return def.textures
  }
  return def.textures[face] ?? def.textures.default ?? ''
}

/**
 * Get UV coordinates for a voxel type and face in the atlas.
 * @param {number} vtype
 * @param {number} face
 * @returns {{u0: number, v0: number, u1: number, v1: number}}
 */
export function getVoxelUvs(vtype, face) {
  const name = resolveTextureName(vtype, face)
  const idx = TEXTURE_NAMES.indexOf(name)
  if (idx < 0) {
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
        TEXTURE_NAMES.map((name) =>
          new Promise((resolve, reject) => {
            loader.load(
              `/assets/voxels/${name}.png`,
              (texture) => {
                resolve({ name, img: texture.image })
              },
              undefined,
              (err) => reject(err)
            )
          })
        )
      ).then((results) => {
        /** @type {Array<{name: string, img: HTMLImageElement}>} */
        const typed = /** @type {any} */ (results)
        // Preserve TEXTURE_NAMES order
        const images = TEXTURE_NAMES.map(n => typed.find(r => r.name === n).img)

        const canvas = buildAtlasCanvas(images)
        voxelTextureAtlas.image = canvas
        voxelTextureAtlas.needsUpdate = true
        voxelTexturesLoaded = true

        loadCallbacks.forEach(cb => cb())
        loadCallbacks.length = 0
      }).catch((err) => {
        console.error('[VoxelTextures] Failed to load voxel textures:', err)
      })
