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

// ── DynamicPositionComponent.updatePrediction / getCurrentPos ───────────────

describe('DynamicPositionComponent.updatePrediction', () => {
  it('returns current position when n = 0', () => {
    const c = new DynamicPositionComponent()
    c.receivedTick = 10; c.receivedX = 100; c.receivedY = 200; c.receivedZ = 300
    c.receivedVx = 5; c.receivedVy = 5; c.receivedVz = 5; c.receivedGrounded = false
    c.updatePrediction(10)
    const p = c.getCurrentPos()
    expect(p.x).toBe(100)
    expect(p.y).toBe(200)
    expect(p.z).toBe(300)
  })

  it('returns current position when n < 0 (past tick)', () => {
    const c = new DynamicPositionComponent()
    c.receivedTick = 50; c.receivedX = 100; c.receivedY = 200; c.receivedZ = 300
    c.receivedVx = 10; c.receivedVy = 10; c.receivedVz = 10; c.receivedGrounded = false
    c.updatePrediction(40)  // 10 ticks before the reference
    const p = c.getCurrentPos()
    expect(p.x).toBe(100)
    expect(p.y).toBe(200)
    expect(p.z).toBe(300)
  })

  it('advances position linearly when grounded (no gravity)', () => {
    const c = new DynamicPositionComponent()
    c.receivedTick = 0; c.receivedX = 0; c.receivedY = 1000; c.receivedZ = 0
    c.receivedVx = 10; c.receivedVy = 0; c.receivedVz = -5; c.receivedGrounded = true
    c.updatePrediction(3)
    const p = c.getCurrentPos()
    expect(p.x).toBe(30)   // 0 + 10*3
    expect(p.y).toBe(1000) // no gravity because grounded
    expect(p.z).toBe(-15)  // 0 + (-5)*3
  })

  it('applies gravity quadratically when airborne (grounded = false)', () => {
    const c = new DynamicPositionComponent()
    c.receivedTick = 0; c.receivedX = 0; c.receivedY = 5000; c.receivedZ = 0
    c.receivedVx = 0; c.receivedVy = 0; c.receivedVz = 0; c.receivedGrounded = false
    const n = 5
    c.updatePrediction(n)
    const p = c.getCurrentPos()
    const expectedGravY = GRAVITY_DECREMENT * n * (n + 1) / 2  // 6*5*6/2 = 90
    expect(p.y).toBe(5000 - expectedGravY)
  })

  it('combines vy and gravity when airborne', () => {
    const c = new DynamicPositionComponent()
    c.receivedTick = 0; c.receivedX = 0; c.receivedY = 0; c.receivedZ = 0
    c.receivedVx = 0; c.receivedVy = 110; c.receivedVz = 0; c.receivedGrounded = false  // jump impulse
    const n = 4
    c.updatePrediction(n)
    const p = c.getCurrentPos()
    // y = vy*n - GRAVITY_DECREMENT*n*(n+1)/2
    const expected = 110 * n - GRAVITY_DECREMENT * n * (n + 1) / 2
    expect(p.y).toBe(expected)
  })

  it('grounded = true suppresses gravity even if vy is non-zero', () => {
    const c = new DynamicPositionComponent()
    c.receivedTick = 0; c.receivedX = 0; c.receivedY = 0; c.receivedZ = 0
    c.receivedVx = 0; c.receivedVy = 50; c.receivedVz = 0; c.receivedGrounded = true
    c.updatePrediction(3)
    const p = c.getCurrentPos()
    expect(p.y).toBe(150)  // 0 + 50*3, no gravity
  })

  it('horizontal axes are always linear regardless of grounded', () => {
    for (const grounded of [true, false]) {
      const c = new DynamicPositionComponent()
      c.receivedTick = 0; c.receivedX = 100; c.receivedY = 500; c.receivedZ = 200
      c.receivedVx = 7; c.receivedVy = 0; c.receivedVz = -3; c.receivedGrounded = grounded
      c.updatePrediction(10)
      const p = c.getCurrentPos()
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
    expect(c.receivedTick).toBe(42)
    expect(c.receivedX).toBe(1000)
    expect(c.receivedY).toBe(2000)
    expect(c.receivedZ).toBe(3000)
    expect(c.receivedVx).toBe(10)
    expect(c.receivedVy).toBe(-6)
    expect(c.receivedVz).toBe(5)
    expect(c.receivedGrounded).toBe(true)
  })

  it('treats grounded byte = 0 as false', () => {
    const bytes = [
      ...i32le(0), ...i32le(0), ...i32le(0),
      ...i32le(0), ...i32le(0), ...i32le(0),
      0,  // grounded = false
    ]
    const c = new DynamicPositionComponent()
    c.deserialize(makeReader(bytes), 1)
    expect(c.receivedGrounded).toBe(false)
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
    expect(entity.motion.receivedTick).toBe(100)
    expect(entity.motion.receivedX).toBe(256)
    expect(entity.motion.receivedY).toBe(512)
    expect(entity.motion.receivedZ).toBe(768)
    expect(entity.motion.receivedVy).toBe(-6)
    expect(entity.motion.receivedGrounded).toBe(false)
  })

  it('leaves motion at defaults when POSITION_BIT is not set', () => {
    const bytes = [
      3, 0, 0, 0,  // GlobalEntityId = 3 (uint32, was uint16)
      0,           // EntityType = PLAYER
      0,           // ComponentFlags = 0 (no components)
    ]
    const entity = BaseEntity.fromRecord(makeReader(bytes), 50)
    expect(entity.id).toBe(3)
    expect(entity.motion.receivedX).toBe(0)
    expect(entity.motion.receivedY).toBe(0)
    expect(entity.motion.receivedZ).toBe(0)
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
    expect(entity.motion.receivedTick).toBe(200)
    expect(entity.motion.receivedX).toBe(1024)
    expect(entity.motion.receivedY).toBe(2048)
    expect(entity.motion.receivedZ).toBe(4096)
    expect(entity.motion.receivedVx).toBe(77)
    expect(entity.motion.receivedVz).toBe(-77)
    expect(entity.motion.receivedGrounded).toBe(true)
  })

  it('does not change motion when no bits are set', () => {
    const entity = new BaseEntity(1, 0)
    entity.motion.receivedX = 999; entity.motion.receivedTick = 5
    entity.applyDelta(makeReader([0x00]), 99)  // flags = 0
    expect(entity.motion.receivedX).toBe(999)
    expect(entity.motion.receivedTick).toBe(5)  // unchanged
  })
})

// ── BaseEntity.currentPos ────────────────────────────────────────────────────

describe('BaseEntity.currentPos', () => {
  it('returns the current predicted position', () => {
    const entity = new BaseEntity(1, 0)
    entity.motion.receivedTick = 10
    entity.motion.receivedX = 500; entity.motion.receivedY = 1000; entity.motion.receivedZ = 0
    entity.motion.receivedVx = 10; entity.motion.receivedVy = 0;  entity.motion.receivedVz = 0
    entity.motion.receivedGrounded = true
    // First update prediction to compute current position
    entity.motion.updatePrediction(13)  // 3 ticks later
    const p = entity.currentPos
    expect(p.x).toBe(530)  // 500 + 10*3
    expect(p.y).toBe(1000)
    expect(p.z).toBe(0)
  })
})

// ── PhysicsPredictionSystem ──────────────────────────────────────────────────

describe('PhysicsPredictionSystem', () => {
  it('can be imported', async () => {
    const { PhysicsPredictionSystem } = await import('../../client/src/systems/PhysicsPredictionSystem.js')
    expect(PhysicsPredictionSystem).toBeDefined()
    expect(typeof PhysicsPredictionSystem.update).toBe('function')
  })
})
