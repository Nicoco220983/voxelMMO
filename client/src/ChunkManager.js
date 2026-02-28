// @ts-check
import * as THREE from 'three'
import LZ4 from 'lz4js'
import {
  CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z, CHUNK_VOXEL_COUNT,
  ChunkMessageType, VoxelType,
} from './types.js'

/**
 * Decompress a raw LZ4 block into a new Uint8Array.
 * @param {Uint8Array} src             Compressed bytes.
 * @param {number}     uncompressedSize Expected output size.
 * @returns {Uint8Array}
 */
function lz4Decompress(src, uncompressedSize) {
  const dst = new Uint8Array(uncompressedSize)
  LZ4.decompressBlock(src, dst, 0, src.length, 0)
  return dst
}

/** @typedef {import('./types.js').ChunkIdPacked} ChunkIdPacked */
/** @typedef {import('./types.js').VoxelType}     VoxelType     */

/**
 * @typedef {Object} ChunkData
 * @property {Uint8Array}       voxels  Flat voxel array (CHUNK_VOXEL_COUNT bytes).
 * @property {THREE.Mesh|null}  mesh    Current Three.js mesh, or null if not yet built.
 * @property {boolean}          dirty   True when voxels changed and mesh needs rebuild.
 */

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
 * @class ChunkManager
 * @description Maintains client-side voxel state and Three.js meshes for all
 * known chunks. Processes incoming chunk-state messages (snapshot / delta)
 * produced by GameClient.
 */
export class ChunkManager {
  /** @type {THREE.Scene} */
  #scene

  /** @type {Map<ChunkIdPacked, ChunkData>} */
  #chunks = new Map()

  /**
   * @param {THREE.Scene} scene  The Three.js scene to add/remove chunk meshes into.
   */
  constructor(scene) {
    this.#scene = scene
  }

  /**
   * Handle one incoming chunk-state message dispatched by GameClient.
   * @param {number}         type     ChunkMessageType value.
   * @param {ChunkIdPacked}  chunkId  Packed ChunkId bigint.
   * @param {DataView}       view     DataView over the full raw message buffer.
   */
  handleChunkMessage(type, chunkId, view) {
    switch (type) {
      case ChunkMessageType.SNAPSHOT_COMPRESSED:
        this.#applySnapshot(chunkId, view)
        break
      case ChunkMessageType.SNAPSHOT_DELTA:
      case ChunkMessageType.TICK_DELTA:
        this.#applyVoxelDelta(chunkId, view, false)
        break
      case ChunkMessageType.SNAPSHOT_DELTA_COMPRESSED:
      case ChunkMessageType.TICK_DELTA_COMPRESSED:
        this.#applyVoxelDelta(chunkId, view, true)
        break
    }
  }

  /**
   * Rebuild Three.js meshes for all chunks that received voxel changes since
   * the last call. Call once per animation frame.
   */
  rebuildDirtyChunks() {
    for (const [cid, data] of this.#chunks) {
      if (data.dirty) {
        this.#rebuildMesh(cid, data)
        data.dirty = false
      }
    }
  }

  /** Remove all chunk meshes and clear internal state. */
  clear() {
    for (const data of this.#chunks.values()) {
      if (data.mesh) this.#scene.remove(data.mesh)
    }
    this.#chunks.clear()
  }

  // ── private helpers ───────────────────────────────────────────────────────

  /**
   * Return the ChunkData for the given id, creating it if absent.
   * @param {ChunkIdPacked} chunkId
   * @returns {ChunkData}
   */
  #getOrCreate(chunkId) {
    let data = this.#chunks.get(chunkId)
    if (!data) {
      data = { voxels: new Uint8Array(CHUNK_VOXEL_COUNT), mesh: null, dirty: false }
      this.#chunks.set(chunkId, data)
    }
    return data
  }

  /**
   * Parse and apply a SNAPSHOT_COMPRESSED message.
   *
   * Wire layout (see ChunkState.hpp for full spec):
   *   [9]       uint8  flags  (bit 0 = entity section LZ4-compressed)
   *   [10:14]   int32  compressed_voxel_size (cvs)
   *   [14:14+cvs]      LZ4( voxels[CHUNK_VOXEL_COUNT] )
   *   [14+cvs:18+cvs]  int32  entity_section_stored_size
   *   if flags & 0x01:
   *     [18+cvs:22+cvs] int32  entity_uncompressed_size
   *     [22+cvs:…]             LZ4( entity_data )
   *   else:
   *     [18+cvs:…]             raw entity_data
   *
   * @param {ChunkIdPacked} chunkId
   * @param {DataView}      view
   */
  #applySnapshot(chunkId, view) {
    const data = this.#getOrCreate(chunkId)
    const raw  = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
    let off = 9  // skip type(1) + chunkId(8)

    const flags = view.getUint8(off++)                        // [9]
    const cvs   = view.getInt32(off, true); off += 4          // [10:14]

    // Decompress voxels directly into data.voxels
    const compressedVoxels = raw.subarray(off, off + cvs)
    const voxels = lz4Decompress(compressedVoxels, CHUNK_VOXEL_COUNT)
    data.voxels.set(voxels)
    off += cvs

    // Entity section (parsed but not yet applied to scene objects)
    const ess = view.getInt32(off, true); off += 4            // entity_section_stored_size
    if (flags & 0x01) {
      // Compressed entity section — decompress and skip for now (entities TODO)
      const entityUncompSize = view.getInt32(off, true); off += 4
      // const entityData = lz4Decompress(raw.subarray(off, off + ess - 4), entityUncompSize)
      off += ess - 4
    } else {
      // Raw entity section — skip for now (entities TODO)
      off += ess
    }

    data.dirty = true
  }

  /**
   * Parse and apply a delta message (snapshot delta or tick delta).
   *
   * Wire layout:
   *   if *_COMPRESSED:
   *     [9:13]  int32  uncompressed_payload_size
   *     [13:]          LZ4( raw_payload )
   *   else (raw_payload directly at [9:]):
   *     int32   voxel_delta_count
   *     (uint16 VoxelId, uint8 VoxelType) × count
   *     int32   entity_delta_count
   *     …entity records (skipped for now)
   *
   * @param {ChunkIdPacked}  chunkId
   * @param {DataView}       view
   * @param {boolean}        compressed
   */
  #applyVoxelDelta(chunkId, view, compressed = false) {
    const data = this.#chunks.get(chunkId)
    if (!data) return   // delta for unknown chunk – wait for snapshot

    const raw = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
    let payload  // Uint8Array of the raw (uncompressed) payload
    let pOff = 0

    if (compressed) {
      const uncompSize = view.getInt32(9, true)
      payload = lz4Decompress(raw.subarray(13), uncompSize)
    } else {
      payload  = raw
      pOff     = 9  // skip header
    }

    const pView = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)

    const count = pView.getInt32(pOff, true); pOff += 4
    for (let i = 0; i < count; i++) {
      const vidPacked = pView.getUint16(pOff, true)
      pOff += 2
      const vtype = pView.getUint8(pOff++)
      const vy  = (vidPacked >> 12) & 0x0f
      const vx  = (vidPacked >>  6) & 0x3f
      const vz  =  vidPacked        & 0x3f
      data.voxels[vy * CHUNK_SIZE_X * CHUNK_SIZE_Z + vx * CHUNK_SIZE_Z + vz] = vtype
    }
    data.dirty = true
  }

  /**
   * (Re)build a Three.js Mesh for the given chunk using naive per-face meshing.
   * Only exposed faces (neighbour is air or out-of-bounds) are emitted.
   * @param {ChunkIdPacked} chunkId
   * @param {ChunkData}     data
   */
  #rebuildMesh(chunkId, data) {
    if (data.mesh) {
      this.#scene.remove(data.mesh)
      data.mesh.geometry.dispose()
      data.mesh = null
    }

    // Decode chunk-grid coordinates from the packed ChunkId
    const packed = chunkId
    const cy = Number(BigInt.asIntN(6,  packed >> 58n))
    const cx = Number(BigInt.asIntN(29, packed >> 29n))
    const cz = Number(BigInt.asIntN(29, packed        ))

    /** @type {number[]} */ const positions = []
    /** @type {number[]} */ const colors    = []
    /** @type {number[]} */ const indices   = []

    const get = (/** @type {number} */ x, /** @type {number} */ y, /** @type {number} */ z) => {
      if (x < 0 || x >= CHUNK_SIZE_X) return 0
      if (y < 0 || y >= CHUNK_SIZE_Y) return 0
      if (z < 0 || z >= CHUNK_SIZE_Z) return 0
      return data.voxels[y * CHUNK_SIZE_X * CHUNK_SIZE_Z + x * CHUNK_SIZE_Z + z]
    }

    for (let y = 0; y < CHUNK_SIZE_Y; y++) {
      for (let x = 0; x < CHUNK_SIZE_X; x++) {
        for (let z = 0; z < CHUNK_SIZE_Z; z++) {
          const vtype = get(x, y, z)
          if (vtype === 0) continue

          const color   = new THREE.Color(VOXEL_COLORS[vtype] ?? 0xffffff)
          /** @type {Array<[number,number,number]>} */
          const offsets = [[1,0,0],[-1,0,0],[0,1,0],[0,-1,0],[0,0,1],[0,0,-1]]

          for (let face = 0; face < 6; face++) {
            const [dx, dy, dz] = offsets[face]
            if (get(x + dx, y + dy, z + dz) !== 0) continue  // face hidden

            const base = positions.length / 3
            for (const [fx, fy, fz] of FACE_VERTS[face]) {
              positions.push(x + fx, y + fy, z + fz)
              colors.push(color.r, color.g, color.b)
            }
            indices.push(base, base + 1, base + 2, base, base + 2, base + 3)
          }
        }
      }
    }

    if (positions.length === 0) return

    const geo = new THREE.BufferGeometry()
    geo.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3))
    geo.setAttribute('color',    new THREE.Float32BufferAttribute(colors,    3))
    geo.setIndex(indices)
    geo.computeVertexNormals()

    const mat  = new THREE.MeshLambertMaterial({ vertexColors: true })
    const mesh = new THREE.Mesh(geo, mat)
    mesh.position.set(cx * CHUNK_SIZE_X, cy * CHUNK_SIZE_Y, cz * CHUNK_SIZE_Z)
    this.#scene.add(mesh)
    data.mesh = mesh
  }
}
