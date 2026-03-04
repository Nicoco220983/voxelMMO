// @ts-check
import * as THREE from 'three'
import { CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z, CHUNK_VOXEL_COUNT, VoxelType, ChunkMessageType } from './types.js'
import { lz4Decompress, BufReader } from './utils.js'

/** @typedef {import('./types.js').ChunkIdPacked} ChunkIdPacked */

/** Voxel-type → 0xRRGGBB colour. AIR (0) is unused (never rendered). */
const VOXEL_COLORS = /** @type {Record<number,number>} */ ({
  [VoxelType.STONE]: 0x888888,
  [VoxelType.DIRT]:  0x8B4513,
  [VoxelType.GRASS]: 0x228B22,
})

/**
 * Face vertex offsets for the 6 axis-aligned cube faces.
 * Order: +X, -X, +Y, -Y, +Z, -Z.
 * Each face has 4 vertices (quad).
 * @type {Array<Array<[number,number,number]>>}
 */
const FACE_VERTS = [
  [[1,0,0],[1,1,0],[1,1,1],[1,0,1]], // +X
  [[0,0,1],[0,1,1],[0,1,0],[0,0,0]], // -X
  [[0,1,0],[0,1,1],[1,1,1],[1,1,0]], // +Y
  [[0,0,1],[0,0,0],[1,0,0],[1,0,1]], // -Y
  [[1,0,1],[1,1,1],[0,1,1],[0,0,1]], // +Z
  [[0,0,0],[0,1,0],[1,1,0],[1,0,0]], // -Z
]

/**
 * Directional shading multiplier per face (same order as FACE_VERTS).
 * Top face is full brightness; bottom is darkest; sides are intermediate.
 */
const FACE_SHADE = [0.75, 0.70, 1.00, 0.55, 0.80, 0.65] // +X -X +Y -Y +Z -Z

/** Neighbour offsets per face — same order as FACE_VERTS / FACE_SHADE. */
const FACE_DIRS = [[1,0,0],[-1,0,0],[0,1,0],[0,-1,0],[0,0,1],[0,0,-1]]

/** Shared material — vertex colours carry all shading; no per-fragment lighting needed. */
const CHUNK_MATERIAL = new THREE.MeshBasicMaterial({ vertexColors: true })

/**
 * Deterministic brightness jitter in [0, 1] from integer world coordinates.
 * Uses a fast integer hash so adjacent voxels look slightly different.
 * @param {number} wx @param {number} wy @param {number} wz
 * @returns {number}
 */
function voxelHash(wx, wy, wz) {
  let h = (Math.imul(wx, 1619) ^ Math.imul(wy, 31337) ^ Math.imul(wz, 6271)) | 0
  h = Math.imul(h ^ (h >>> 16), 0x45d9f3b)
  return (h >>> 24) / 255  // top 8 bits → 0..1
}

/**
 * @class Chunk
 * @description Client-side state for one chunk: voxel data and Three.js mesh.
 * Entity management is handled by EntityRegistry; Chunk only manages voxels.
 */
export class Chunk {
  /** @type {ChunkIdPacked} */
  chunkId

  /** @type {Uint8Array} */
  #voxels

  /** @type {THREE.Mesh|null} */
  #mesh = null

  /** @type {boolean} True when voxels changed and mesh needs rebuild. */
  dirty = false

  /** @returns {Uint8Array} */
  get voxels() { return this.#voxels }

  /** @param {ChunkIdPacked} chunkId */
  constructor(chunkId) {
    this.chunkId = chunkId
    this.#voxels = new Uint8Array(CHUNK_VOXEL_COUNT)
  }

  /**
   * Set all voxels at once (from decompressed snapshot).
   * @param {Uint8Array} voxelData
   */
  setVoxels(voxelData) {
    this.#voxels.set(voxelData)
  }

  /**
   * Set a single voxel.
   * @param {number} x Local x ∈ [0, CHUNK_SIZE_X)
   * @param {number} y Local y ∈ [0, CHUNK_SIZE_Y)
   * @param {number} z Local z ∈ [0, CHUNK_SIZE_Z)
   * @param {number} vtype VoxelType
   */
  setVoxel(x, y, z, vtype) {
    this.#voxels[y * CHUNK_SIZE_X * CHUNK_SIZE_Z + x * CHUNK_SIZE_Z + z] = vtype
  }

  /**
   * Returns the voxel type at local chunk coordinates.
   * @param {number} x  Local x ∈ [0, CHUNK_SIZE_X)
   * @param {number} y  Local y ∈ [0, CHUNK_SIZE_Y)
   * @param {number} z  Local z ∈ [0, CHUNK_SIZE_Z)
   * @returns {number} VoxelType byte (0 = AIR)
   */
  getVoxel(x, y, z) {
    return this.#voxels[y * CHUNK_SIZE_X * CHUNK_SIZE_Z + x * CHUNK_SIZE_Z + z]
  }

  /**
   * (Re)build the Three.js mesh for this chunk using naive per-face meshing.
   * Adds the new mesh to scene and removes the previous one if any.
   * Clears the dirty flag when done.
   * @param {THREE.Scene} scene
   */
  rebuildMesh(scene) {
    if (this.#mesh) {
      scene.remove(this.#mesh)
      this.#mesh.geometry.dispose()
      this.#mesh = null
    }

    const cy = Number(BigInt.asIntN(6,  this.chunkId >> 58n))
    const cx = Number(BigInt.asIntN(29, this.chunkId >> 29n))
    const cz = Number(BigInt.asIntN(29, this.chunkId))

    /** @type {number[]} */ const positions = []
    /** @type {number[]} */ const colors    = []
    /** @type {number[]} */ const indices   = []

    const get = (/** @type {number} */ x, /** @type {number} */ y, /** @type {number} */ z) => {
      if (x < 0 || x >= CHUNK_SIZE_X) return 0
      if (y < 0 || y >= CHUNK_SIZE_Y) return 0
      if (z < 0 || z >= CHUNK_SIZE_Z) return 0
      return this.#voxels[y * CHUNK_SIZE_X * CHUNK_SIZE_Z + x * CHUNK_SIZE_Z + z]
    }

    for (let y = 0; y < CHUNK_SIZE_Y; y++) {
      for (let x = 0; x < CHUNK_SIZE_X; x++) {
        for (let z = 0; z < CHUNK_SIZE_Z; z++) {
          const vtype = get(x, y, z)
          if (vtype === 0) continue

          // Decode base RGB from packed hex (avoids THREE.Color allocation per voxel)
          const hex = VOXEL_COLORS[vtype] ?? 0xffffff
          const br  = ((hex >>> 16) & 0xFF) / 255
          const bg  = ((hex >>>  8) & 0xFF) / 255
          const bb  = ( hex         & 0xFF) / 255

          // Per-voxel brightness jitter [0.88 .. 1.12] from world position hash
          const jitter = 0.88 + 0.24 * voxelHash(cx * CHUNK_SIZE_X + x,
                                                   cy * CHUNK_SIZE_Y + y,
                                                   cz * CHUNK_SIZE_Z + z)

          for (let face = 0; face < 6; face++) {
            const [dx, dy, dz] = FACE_DIRS[face]
            if (get(x + dx, y + dy, z + dz) !== 0) continue

            const shade = FACE_SHADE[face] * jitter
            const r = br * shade, g = bg * shade, b = bb * shade

            const base = positions.length / 3
            for (const [fx, fy, fz] of FACE_VERTS[face]) {
              positions.push(x + fx, y + fy, z + fz)
              colors.push(r, g, b)
            }
            indices.push(base, base + 1, base + 2, base, base + 2, base + 3)
          }
        }
      }
    }

    this.dirty = false

    if (positions.length === 0) return

    const geo = new THREE.BufferGeometry()
    geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3))
    geo.setAttribute('color',    new THREE.Float32BufferAttribute(colors,    3))
    geo.setIndex(indices)

    this.#mesh = new THREE.Mesh(geo, CHUNK_MATERIAL)
    this.#mesh.position.set(cx * CHUNK_SIZE_X, cy * CHUNK_SIZE_Y, cz * CHUNK_SIZE_Z)
    scene.add(this.#mesh)
  }

  /**
   * Remove this chunk's mesh from the scene and release GPU resources.
   * @param {THREE.Scene} scene
   */
  dispose(scene) {
    if (this.#mesh) {
      scene.remove(this.#mesh)
      this.#mesh.geometry.dispose()
      this.#mesh = null
    }
  }

  // ── Legacy API for tests and backward compatibility ────────────────────────

  /**
   * Parse and apply a SNAPSHOT_COMPRESSED message (voxels only).
   * Note: entity parsing is now handled by EntityRegistry.
   * @param {DataView} view         View over the full raw message buffer.
   * @param {number}   messageTick  Server tick embedded in the message header.
   */
  applySnapshot(view, messageTick) {
    const raw = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
    let off = 13  // skip type(1) + chunkId(8) + tick(4)

    const flags = view.getUint8(off++)
    const cvs   = view.getInt32(off, true); off += 4

    this.#voxels.set(lz4Decompress(raw.subarray(off, off + cvs), CHUNK_VOXEL_COUNT))
    off += cvs

    // Skip entity section (handled by EntityRegistry)
    const ess = view.getInt32(off, true)
    // off += ess + 4  // Not needed, just ignore

    this.dirty = true
  }

  /**
   * Parse and apply a delta message (voxels only).
   * Note: entity parsing is now handled by EntityRegistry.
   * @param {DataView} view         View over the full raw message buffer.
   * @param {boolean}  compressed   True when the payload is LZ4-compressed.
   * @param {number}   messageTick  Server tick embedded in the message header.
   */
  applyVoxelDelta(view, compressed = false, messageTick = 0) {
    const raw = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
    let payload, pOff = 0

    if (compressed) {
      const uncompSize = view.getInt32(13, true)
      payload = lz4Decompress(raw.subarray(17), uncompSize)
    } else {
      payload = raw
      pOff    = 13
    }

    const pView = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
    const count = pView.getInt32(pOff, true); pOff += 4
    for (let i = 0; i < count; i++) {
      const vidPacked = pView.getUint16(pOff, true); pOff += 2
      const vtype     = pView.getUint8(pOff++)
      const vy = (vidPacked >> 10) & 0x1f
      const vx = (vidPacked >>  5) & 0x1f
      const vz =  vidPacked        & 0x1f
      this.#voxels[vy * CHUNK_SIZE_X * CHUNK_SIZE_Z + vx * CHUNK_SIZE_Z + vz] = vtype
    }

    // Entity section is skipped (handled by EntityRegistry)

    this.dirty = true
  }
}
