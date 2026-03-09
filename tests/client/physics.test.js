// @ts-check
import { describe, it, expect } from 'vitest'
import { DynamicPositionComponent } from '../../client/src/components/DynamicPositionComponent.js'
import { BaseEntity } from '../../client/src/entities/BaseEntity.js'
import { GRAVITY_DECREMENT, POSITION_BIT } from '../../client/src/types.js'

// ── BufReader helper (no lz4js dependency needed for physics tests) ──────────

/**
 * Build a DataView + BufReader-like object from a byte array.
 * @param {number[]} bytes
 */
function makeReader(bytes) {
  const buf  = new ArrayBuffer(bytes.length)
  const view = new DataView(buf)
  bytes.forEach((b, i) => view.setUint8(i, b))
  let off = 0
  return {
    get offset() { return off },
    readUint8()   { return view.getUint8(off++) },
    readUint16()  { const v = view.getUint16(off, true); off += 2; return v },
    readInt32()   { const v = view.getInt32(off, true);  off += 4; return v },
    readUint32()  { const v = view.getUint32(off, true); off += 4; return v },
    readFloat32() { const v = view.getFloat32(off, true); off += 4; return v },
  }
}

/**
 * Encode an int32 as 4 little-endian bytes.
 * @param {number} v
 * @returns {number[]}
 */
function i32le(v) {
  const b = new ArrayBuffer(4)
  new DataView(b).setInt32(0, v, true)
  return [...new Uint8Array(b)]
}

// ── DynamicPositionComponent.getPos ────────────────────────────────────────

describe('DynamicPositionComponent.getPos', () => {
  it('returns current position when n = 0', () => {
    const c = new DynamicPositionComponent()
    c.tick = 10; c.x = 100; c.y = 200; c.z = 300
    c.vx = 5; c.vy = 5; c.vz = 5; c.grounded = false
    const p = c.getPos(10)
    expect(p.x).toBe(100)
    expect(p.y).toBe(200)
    expect(p.z).toBe(300)
  })

  it('returns current position when n < 0 (past tick)', () => {
    const c = new DynamicPositionComponent()
    c.tick = 50; c.x = 100; c.y = 200; c.z = 300
    c.vx = 10; c.vy = 10; c.vz = 10; c.grounded = false
    const p = c.getPos(40)  // 10 ticks before the reference
    expect(p.x).toBe(100)
    expect(p.y).toBe(200)
    expect(p.z).toBe(300)
  })

  it('advances position linearly when grounded (no gravity)', () => {
    const c = new DynamicPositionComponent()
    c.tick = 0; c.x = 0; c.y = 1000; c.z = 0
    c.vx = 10; c.vy = 0; c.vz = -5; c.grounded = true
    const p = c.getPos(3)
    expect(p.x).toBe(30)   // 0 + 10*3
    expect(p.y).toBe(1000) // no gravity because grounded
    expect(p.z).toBe(-15)  // 0 + (-5)*3
  })

  it('applies gravity quadratically when airborne (grounded = false)', () => {
    const c = new DynamicPositionComponent()
    c.tick = 0; c.x = 0; c.y = 5000; c.z = 0
    c.vx = 0; c.vy = 0; c.vz = 0; c.grounded = false
    const n = 5
    const p = c.getPos(n)
    const expectedGravY = GRAVITY_DECREMENT * n * (n + 1) / 2  // 6*5*6/2 = 90
    expect(p.y).toBe(5000 - expectedGravY)
  })

  it('combines vy and gravity when airborne', () => {
    const c = new DynamicPositionComponent()
    c.tick = 0; c.x = 0; c.y = 0; c.z = 0
    c.vx = 0; c.vy = 110; c.vz = 0; c.grounded = false  // jump impulse
    const n = 4
    const p = c.getPos(n)
    // y = vy*n - GRAVITY_DECREMENT*n*(n+1)/2
    const expected = 110 * n - GRAVITY_DECREMENT * n * (n + 1) / 2
    expect(p.y).toBe(expected)
  })

  it('grounded = true suppresses gravity even if vy is non-zero', () => {
    const c = new DynamicPositionComponent()
    c.tick = 0; c.x = 0; c.y = 0; c.z = 0
    c.vx = 0; c.vy = 50; c.vz = 0; c.grounded = true
    const p = c.getPos(3)
    expect(p.y).toBe(150)  // 0 + 50*3, no gravity
  })

  it('horizontal axes are always linear regardless of grounded', () => {
    for (const grounded of [true, false]) {
      const c = new DynamicPositionComponent()
      c.tick = 0; c.x = 100; c.y = 500; c.z = 200
      c.vx = 7; c.vy = 0; c.vz = -3; c.grounded = grounded
      const p = c.getPos(10)
      expect(p.x).toBe(100 + 7 * 10)
      expect(p.z).toBe(200 + (-3) * 10)
    }
  })
})

// ── DynamicPositionComponent.deserialize ─────────────────────────────────────

describe('DynamicPositionComponent.deserialize', () => {
  it('reads x,y,z, vx,vy,vz, grounded from reader', () => {
    const bytes = [
      ...i32le(1000), ...i32le(2000), ...i32le(3000),  // x, y, z
      ...i32le(10),   ...i32le(-6),   ...i32le(5),      // vx, vy, vz
      1,                                                  // grounded = true
    ]
    const reader = makeReader(bytes)
    const c = new DynamicPositionComponent()
    c.deserialize(reader, 42)
    expect(c.tick).toBe(42)
    expect(c.x).toBe(1000)
    expect(c.y).toBe(2000)
    expect(c.z).toBe(3000)
    expect(c.vx).toBe(10)
    expect(c.vy).toBe(-6)
    expect(c.vz).toBe(5)
    expect(c.grounded).toBe(true)
  })

  it('treats grounded byte = 0 as false', () => {
    const bytes = [
      ...i32le(0), ...i32le(0), ...i32le(0),
      ...i32le(0), ...i32le(0), ...i32le(0),
      0,  // grounded = false
    ]
    const c = new DynamicPositionComponent()
    c.deserialize(makeReader(bytes), 1)
    expect(c.grounded).toBe(false)
  })
})

// ── BaseEntity.fromRecord ─────────────────────────────────────────────────────

describe('BaseEntity.fromRecord', () => {
  it('reads id, type, flags and deserializes POSITION_BIT component', () => {
    const bytes = [
      // GlobalEntityId uint32 LE (was uint16)
      5, 0, 0, 0,
      // EntityType uint8
      1,  // GHOST_PLAYER
      // ComponentFlags uint8
      POSITION_BIT,
      // DynamicPositionComponent: x,y,z,vx,vy,vz,grounded
      ...i32le(256), ...i32le(512), ...i32le(768),
      ...i32le(0),   ...i32le(-6),  ...i32le(0),
      0,  // grounded = false
    ]
    const entity = BaseEntity.fromRecord(makeReader(bytes), 100)
    expect(entity.id).toBe(5)
    expect(entity.type).toBe(1)
    expect(entity.motion.tick).toBe(100)
    expect(entity.motion.x).toBe(256)
    expect(entity.motion.y).toBe(512)
    expect(entity.motion.z).toBe(768)
    expect(entity.motion.vy).toBe(-6)
    expect(entity.motion.grounded).toBe(false)
  })

  it('leaves motion at defaults when POSITION_BIT is not set', () => {
    const bytes = [
      3, 0, 0, 0,  // GlobalEntityId = 3 (uint32, was uint16)
      0,           // EntityType = PLAYER
      0,           // ComponentFlags = 0 (no components)
    ]
    const entity = BaseEntity.fromRecord(makeReader(bytes), 50)
    expect(entity.id).toBe(3)
    expect(entity.motion.x).toBe(0)
    expect(entity.motion.y).toBe(0)
    expect(entity.motion.z).toBe(0)
  })
})

// ── BaseEntity.applyDelta ─────────────────────────────────────────────────────

describe('BaseEntity.applyDelta', () => {
  it('updates motion from delta record', () => {
    const entity = new BaseEntity(7, 0)
    const bytes = [
      POSITION_BIT,
      ...i32le(1024), ...i32le(2048), ...i32le(4096),
      ...i32le(77),   ...i32le(0),    ...i32le(-77),
      1,  // grounded = true
    ]
    entity.applyDelta(makeReader(bytes), 200)
    expect(entity.motion.tick).toBe(200)
    expect(entity.motion.x).toBe(1024)
    expect(entity.motion.y).toBe(2048)
    expect(entity.motion.z).toBe(4096)
    expect(entity.motion.vx).toBe(77)
    expect(entity.motion.vz).toBe(-77)
    expect(entity.motion.grounded).toBe(true)
  })

  it('does not change motion when no bits are set', () => {
    const entity = new BaseEntity(1, 0)
    entity.motion.x = 999; entity.motion.tick = 5
    entity.applyDelta(makeReader([0x00]), 99)  // flags = 0
    expect(entity.motion.x).toBe(999)
    expect(entity.motion.tick).toBe(5)  // unchanged
  })
})

// ── BaseEntity.getPos ──────────────────────────────────────────────────────

describe('BaseEntity.getPos', () => {
  it('delegates to motion.getPos', () => {
    const entity = new BaseEntity(1, 0)
    entity.motion.tick = 10
    entity.motion.x = 500; entity.motion.y = 1000; entity.motion.z = 0
    entity.motion.vx = 10; entity.motion.vy = 0;  entity.motion.vz = 0
    entity.motion.grounded = true
    const p = entity.getPos(13)
    expect(p.x).toBe(530)  // 500 + 10*3
    expect(p.y).toBe(1000)
    expect(p.z).toBe(0)
  })
})
