// @ts-check
import { describe, it, expect, vi } from 'vitest'

// Mock Three.js — we never call rebuildMesh in these tests.
vi.mock('three', () => ({
  Mesh: class { position = { set() {} } },
  BufferGeometry: class {
    setAttribute() {}
    setIndex() {}
    computeVertexNormals() {}
    dispose() {}
  },
  Float32BufferAttribute: class {},
  MeshLambertMaterial: class {},
}))

// Mock utils.js — lz4Decompress is replaced with a passthrough that copies src
// into a full-size output buffer.  This avoids the CJS/ESM resolution issue
// that lz4js triggers inside vitest and keeps tests focused on the parsing logic.
vi.mock('../../client/src/utils.js', () => ({
  lz4Decompress: (/** @type {Uint8Array} */ src, /** @type {number} */ uncompressedSize) => {
    const dst = new Uint8Array(uncompressedSize)
    dst.set(src.subarray(0, Math.min(src.length, uncompressedSize)))
    return dst
  },
  BufReader: class {
    #view; offset = 0
    constructor(/** @type {DataView} */ v, off = 0) { this.#view = v; this.offset = off }
    readUint8()   { return this.#view.getUint8(this.offset++) }
    readUint16()  { const v = this.#view.getUint16(this.offset, true); this.offset += 2; return v }
    readInt32()   { const v = this.#view.getInt32(this.offset,  true); this.offset += 4; return v }
    readUint32()  { const v = this.#view.getUint32(this.offset, true); this.offset += 4; return v }
    readFloat32() { const v = this.#view.getFloat32(this.offset, true); this.offset += 4; return v }
  },
}))

import { Chunk } from '../../client/src/Chunk.js'
import {
  CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z, CHUNK_VOXEL_COUNT,
  VoxelType, voxelIdFromVoxelPos, getVoxelPos,
} from '../../client/src/types.js'
import { ServerMessageType } from '../../client/src/NetworkProtocol.js'

// ── Helpers ──────────────────────────────────────────────────────────────────

/** Pack ChunkId as BigInt: sint6(y) | sint29(x) | sint29(z). */
function packChunkId(y, x, z) {
  return (BigInt(y & 0x3F) << 58n)
       | (BigInt(x & 0x1FFFFFFF) << 29n)
       |  BigInt(z & 0x1FFFFFFF)
}

/**
 * Build a CHUNK_SNAPSHOT_COMPRESSED message using raw voxels as the "compressed"
 * payload.  The lz4Decompress mock copies src into a full CHUNK_VOXEL_COUNT
 * buffer, so passing raw voxels here produces correct decompressed output.
 *
 * New format: [type(1)][size(2)][chunkId(8)][tick(4)][flags(1)][cvs(4)][voxels][ess(4)]
 */
function buildSnapshotMsg(chunkId, tick, voxels) {
  const cvs = voxels.length  // "compressed size" == raw size (no real compression)

  // type(1) + size(2) + chunkId(8) + tick(4) + flags(1) + cvs(4) + raw_voxels + ess(4)
  const totalSize = 1 + 2 + 8 + 4 + 1 + 4 + cvs + 4
  const buf = new ArrayBuffer(totalSize)
  const view = new DataView(buf)
  const raw = new Uint8Array(buf)
  let off = 0

  view.setUint8(off++, ServerMessageType.CHUNK_SNAPSHOT_COMPRESSED)
  view.setUint16(off, totalSize, true); off += 2  // size field
  view.setBigInt64(off, chunkId, true); off += 8
  view.setUint32(off, tick, true);      off += 4
  view.setUint8(off++, 0)               // flags = 0 (entity section uncompressed)
  view.setInt32(off, cvs, true);        off += 4
  raw.set(voxels, off);                 off += cvs
  view.setInt32(off, 0, true)           // entity_section_size = 0

  return view
}

/**
 * Build an uncompressed CHUNK_TICK_DELTA or CHUNK_SNAPSHOT_DELTA message.
 * @param {bigint} chunkId
 * @param {number} tick
 * @param {Array<{vy:number,vx:number,vz:number,vtype:number}>} mods
 * @param {number} [msgType]
 *
 * New format: [type(1)][size(2)][chunkId(8)][tick(4)][count(4)][mods...]
 */
function buildDeltaMsg(chunkId, tick, mods, msgType = ServerMessageType.CHUNK_TICK_DELTA) {
  // type(1) + size(2) + chunkId(8) + tick(4) + count(4) + mods*(vid_u16 + vtype_u8)
  const totalSize = 15 + 4 + mods.length * 3
  const buf = new ArrayBuffer(totalSize)
  const view = new DataView(buf)
  let off = 0

  view.setUint8(off++, msgType)
  view.setUint16(off, totalSize, true); off += 2  // size field
  view.setBigInt64(off, chunkId, true); off += 8
  view.setUint32(off, tick, true);      off += 4
  view.setInt32(off, mods.length, true); off += 4
  for (const { vy, vx, vz, vtype } of mods) {
    view.setUint16(off, voxelIdFromVoxelPos(vx, vy, vz), true); off += 2
    view.setUint8(off++, vtype)
  }

  return view
}

// ── Types: packing roundtrip ──────────────────────────────────────────────────

describe('ChunkId packing', () => {
  it('roundtrips y/x/z components', () => {
    const cases = [
      { y: 0,   x: 0,  z: 0 },
      { y: 1,   x: 1,  z: 1 },
      { y: -1,  x: -1, z: -1 },
      { y: 31,  x: 268435455,  z: 268435455 },
      { y: -32, x: -268435456, z: -268435456 },
    ]
    for (const { y, x, z } of cases) {
      const packed = packChunkId(y, x, z)
      const dy = Number(BigInt.asIntN(6,  packed >> 58n))
      const dx = Number(BigInt.asIntN(29, packed >> 29n))
      const dz = Number(BigInt.asIntN(29, packed))
      expect(dy).toBe(y)
      expect(dx).toBe(x)
      expect(dz).toBe(z)
    }
  })
})

describe('VoxelId packing', () => {
  it('roundtrips y/x/z components', () => {
    const cases = [
      { y: 0,  x: 0,  z: 0 },
      { y: 31, x: 31, z: 31 },
      { y: 5,  x: 10, z: 20 },
      { y: 3,  x: 7,  z: 7 },
    ]
    for (const { y, x, z } of cases) {
      const packed = voxelIdFromVoxelPos(x, y, z)
      const { vx, vy, vz } = getVoxelPos(packed)
      expect(vy).toBe(y)
      expect(vx).toBe(x)
      expect(vz).toBe(z)
    }
  })
})

// ── Chunk.applySnapshot ───────────────────────────────────────────────────────

describe('Chunk.applySnapshot', () => {
  const chunkId = packChunkId(0, 0, 0)

  it('sets dirty flag', () => {
    const voxels = new Uint8Array(CHUNK_VOXEL_COUNT)
    const chunk = new Chunk(chunkId)
    expect(chunk.dirty).toBe(false)
    chunk.applySnapshot(buildSnapshotMsg(chunkId, 1, voxels), 1)
    expect(chunk.dirty).toBe(true)
  })

  it('populates voxels correctly', () => {
    const voxels = new Uint8Array(CHUNK_VOXEL_COUNT)
    voxels[5 * CHUNK_SIZE_X * CHUNK_SIZE_Z + 10 * CHUNK_SIZE_Z + 20] = VoxelType.GRASS
    voxels[3 * CHUNK_SIZE_X * CHUNK_SIZE_Z +  7 * CHUNK_SIZE_Z +  7] = VoxelType.STONE

    const chunk = new Chunk(chunkId)
    chunk.applySnapshot(buildSnapshotMsg(chunkId, 42, voxels), 42)

    expect(chunk.getVoxel(10, 5, 20)).toBe(VoxelType.GRASS)
    expect(chunk.getVoxel(7,  3,  7)).toBe(VoxelType.STONE)
    expect(chunk.getVoxel(0,  0,  0)).toBe(VoxelType.AIR)
  })

  it('all-zero snapshot leaves all voxels as AIR', () => {
    const voxels = new Uint8Array(CHUNK_VOXEL_COUNT)  // all zeros = AIR
    const chunk = new Chunk(chunkId)
    chunk.applySnapshot(buildSnapshotMsg(chunkId, 1, voxels), 1)

    for (let y = 0; y < CHUNK_SIZE_Y; y++) {
      for (let x = 0; x < CHUNK_SIZE_X; x++) {
        for (let z = 0; z < CHUNK_SIZE_Z; z++) {
          expect(chunk.getVoxel(x, y, z)).toBe(VoxelType.AIR)
        }
      }
    }
  })

  it('second applySnapshot overwrites previous voxels', () => {
    const v1 = new Uint8Array(CHUNK_VOXEL_COUNT)
    v1[0] = VoxelType.STONE
    const v2 = new Uint8Array(CHUNK_VOXEL_COUNT)
    v2[0] = VoxelType.DIRT

    const chunk = new Chunk(chunkId)
    chunk.applySnapshot(buildSnapshotMsg(chunkId, 1, v1), 1)
    expect(chunk.getVoxel(0, 0, 0)).toBe(VoxelType.STONE)
    chunk.applySnapshot(buildSnapshotMsg(chunkId, 2, v2), 2)
    expect(chunk.getVoxel(0, 0, 0)).toBe(VoxelType.DIRT)
  })
})

// ── Chunk.applyVoxelDelta ─────────────────────────────────────────────────────

describe('Chunk.applyVoxelDelta', () => {
  const chunkId = packChunkId(0, 2, -1)

  it('sets dirty flag', () => {
    const view = buildDeltaMsg(chunkId, 5, [{ vy: 1, vx: 2, vz: 3, vtype: VoxelType.DIRT }])
    const chunk = new Chunk(chunkId)
    expect(chunk.dirty).toBe(false)
    chunk.applyVoxelDelta(view, false, 5)
    expect(chunk.dirty).toBe(true)
  })

  it('applies single voxel change', () => {
    const view = buildDeltaMsg(chunkId, 7, [{ vy: 5, vx: 10, vz: 20, vtype: VoxelType.GRASS }])
    const chunk = new Chunk(chunkId)
    chunk.applyVoxelDelta(view, false, 7)
    expect(chunk.getVoxel(10, 5, 20)).toBe(VoxelType.GRASS)
    expect(chunk.getVoxel(0, 0, 0)).toBe(VoxelType.AIR)
  })

  it('applies multiple voxel changes', () => {
    const mods = [
      { vy: 0, vx: 0,  vz: 0,  vtype: VoxelType.STONE },
      { vy: 5, vx: 31, vz: 31, vtype: VoxelType.DIRT  },
      { vy: 9, vx: 16, vz: 16, vtype: VoxelType.GRASS },
    ]
    const chunk = new Chunk(chunkId)
    chunk.applyVoxelDelta(buildDeltaMsg(chunkId, 10, mods), false, 10)

    expect(chunk.getVoxel(0,  0, 0 )).toBe(VoxelType.STONE)
    expect(chunk.getVoxel(31, 5, 31)).toBe(VoxelType.DIRT)
    expect(chunk.getVoxel(16, 9, 16)).toBe(VoxelType.GRASS)
  })

  it('overwrites a previously set voxel', () => {
    const cid = packChunkId(0, 0, 0)
    const chunk = new Chunk(cid)

    chunk.applyVoxelDelta(buildDeltaMsg(cid, 1, [{ vy: 2, vx: 4, vz: 8, vtype: VoxelType.GRASS }]), false, 1)
    expect(chunk.getVoxel(4, 2, 8)).toBe(VoxelType.GRASS)

    chunk.applyVoxelDelta(buildDeltaMsg(cid, 2, [{ vy: 2, vx: 4, vz: 8, vtype: VoxelType.AIR }]), false, 2)
    expect(chunk.getVoxel(4, 2, 8)).toBe(VoxelType.AIR)
  })

  it('empty delta changes nothing', () => {
    const chunk = new Chunk(chunkId)
    chunk.applyVoxelDelta(buildDeltaMsg(chunkId, 3, []), false, 3)
    expect(chunk.dirty).toBe(true)
    expect(chunk.getVoxel(0, 0, 0)).toBe(VoxelType.AIR)
  })
})

// ── Batch message parsing ─────────────────────────────────────────────────────

describe('batch framing (length-prefixed messages)', () => {
  it('correctly splits two concatenated deltas', () => {
    const cid = packChunkId(0, 0, 0)
    const v1 = buildDeltaMsg(cid, 1, [{ vy: 1, vx: 1, vz: 1, vtype: VoxelType.STONE }])
    const v2 = buildDeltaMsg(cid, 2, [{ vy: 2, vx: 2, vz: 2, vtype: VoxelType.GRASS }])

    const len1 = v1.byteLength
    const len2 = v2.byteLength
    const batch = new ArrayBuffer(4 + len1 + 4 + len2)
    const batchView = new DataView(batch)
    const batchRaw  = new Uint8Array(batch)

    batchView.setUint32(0, len1, true)
    batchRaw.set(new Uint8Array(v1.buffer, v1.byteOffset, len1), 4)
    batchView.setUint32(4 + len1, len2, true)
    batchRaw.set(new Uint8Array(v2.buffer, v2.byteOffset, len2), 4 + len1 + 4)

    const chunk = new Chunk(cid)
    let off = 0
    while (off + 4 <= batch.byteLength) {
      const msgLen = batchView.getUint32(off, true)
      off += 4
      chunk.applyVoxelDelta(new DataView(batch, off, msgLen), false, 0)
      off += msgLen
    }

    expect(chunk.getVoxel(1, 1, 1)).toBe(VoxelType.STONE)
    expect(chunk.getVoxel(2, 2, 2)).toBe(VoxelType.GRASS)
  })
})

// ── ServerMessageType enum values ─────────────────────────────────────────────

describe('ServerMessageType enum', () => {
  it('has correct values for chunk state messages', () => {
    expect(ServerMessageType.CHUNK_SNAPSHOT).toBe(0)
    expect(ServerMessageType.CHUNK_SNAPSHOT_COMPRESSED).toBe(1)
    expect(ServerMessageType.CHUNK_SNAPSHOT_DELTA).toBe(2)
    expect(ServerMessageType.CHUNK_SNAPSHOT_DELTA_COMPRESSED).toBe(3)
    expect(ServerMessageType.CHUNK_TICK_DELTA).toBe(4)
    expect(ServerMessageType.CHUNK_TICK_DELTA_COMPRESSED).toBe(5)
    expect(ServerMessageType.SELF_ENTITY).toBe(6)
  })
})
