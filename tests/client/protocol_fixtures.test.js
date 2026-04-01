// @ts-check
import { describe, it, expect } from 'vitest'
import { NetworkProtocol, ClientMessageType, ServerMessageType, InputButton, InputType } from '../../client/src/NetworkProtocol.js'
import { EntityType } from '../../client/src/types.js'
import { 
  loadHexFixture, 
  loadInputFixture, 
  loadJoinFixture, 
  loadSelfEntityFixture,
  bytesEqual,
  formatBytes 
} from '../protocol_fixtures/FixtureLoader.js'

// ── serializeInput matches fixtures ──────────────────────────────────────────

describe('NetworkProtocol.serializeInput matches fixtures', () => {
  it('serializes zero input matching input_zero.hex', () => {
    const expected = loadHexFixture('client_to_server/input/input_zero.hex')
    const actual = new Uint8Array(NetworkProtocol.serializeInput(InputType.MOVE, 0, 0, 0))
    
    expect(actual.length).toBe(expected.length)
    expect(bytesEqual(actual, expected)).toBe(true)
  })

  it('serializes forward button matching input_forward.hex', () => {
    const expected = loadHexFixture('client_to_server/input/input_forward.hex')
    const actual = new Uint8Array(NetworkProtocol.serializeInput(InputType.MOVE, InputButton.FORWARD, 0, 0))
    
    expect(actual.length).toBe(expected.length)
    expect(bytesEqual(actual, expected)).toBe(true)
  })

  it('serializes jump button matching input_jump.hex', () => {
    const expected = loadHexFixture('client_to_server/input/input_jump.hex')
    const actual = new Uint8Array(NetworkProtocol.serializeInput(InputType.MOVE, InputButton.JUMP, 0, 0))
    
    expect(actual.length).toBe(expected.length)
    expect(bytesEqual(actual, expected)).toBe(true)
  })

  it('serializes all buttons matching input_all_buttons.hex', () => {
    const expected = loadHexFixture('client_to_server/input/input_all_buttons.hex')
    const allButtons = InputButton.FORWARD | InputButton.BACKWARD | 
                       InputButton.LEFT | InputButton.RIGHT | 
                       InputButton.JUMP | InputButton.DESCEND
    const actual = new Uint8Array(NetworkProtocol.serializeInput(InputType.MOVE, allButtons, 0, 0))
    
    expect(actual.length).toBe(expected.length)
    expect(bytesEqual(actual, expected)).toBe(true)
  })

  it('serializes yaw/pitch matching input_yaw_pitch.hex', () => {
    const expected = loadHexFixture('client_to_server/input/input_yaw_pitch.hex')
    const actual = new Uint8Array(NetworkProtocol.serializeInput(InputType.MOVE, 0, 1.0, 2.0))
    
    expect(actual.length).toBe(expected.length)
    expect(bytesEqual(actual, expected)).toBe(true)
  })

  it('serializes complex input matching input_complex.hex', () => {
    const expected = loadHexFixture('client_to_server/input/input_complex.hex')
    const buttons = InputButton.FORWARD | InputButton.LEFT
    const actual = new Uint8Array(NetworkProtocol.serializeInput(InputType.MOVE, buttons, 3.14159, -0.5))
    
    expect(actual.length).toBe(expected.length)
    expect(bytesEqual(actual, expected)).toBe(true)
  })
})

// ── serializeJoin matches fixtures ───────────────────────────────────────────

describe('NetworkProtocol.serializeJoin matches fixtures', () => {
  const zeroToken = new Uint8Array(16)  // All zeros

  it('serializes PLAYER join matching join_player.hex', () => {
    const expected = loadHexFixture('client_to_server/join/join_player.hex')
    const actual = new Uint8Array(NetworkProtocol.serializeJoin(EntityType.PLAYER, zeroToken))
    
    expect(actual.length).toBe(expected.length)
    expect(bytesEqual(actual, expected)).toBe(true)
  })

  it('serializes GHOST_PLAYER join matching join_ghost.hex', () => {
    const expected = loadHexFixture('client_to_server/join/join_ghost.hex')
    const actual = new Uint8Array(NetworkProtocol.serializeJoin(EntityType.GHOST_PLAYER, zeroToken))
    
    expect(actual.length).toBe(expected.length)
    expect(bytesEqual(actual, expected)).toBe(true)
  })

  it('serializes SHEEP join matching join_sheep.hex', () => {
    const expected = loadHexFixture('client_to_server/join/join_sheep.hex')
    const actual = new Uint8Array(NetworkProtocol.serializeJoin(EntityType.SHEEP, zeroToken))
    
    expect(actual.length).toBe(expected.length)
    expect(bytesEqual(actual, expected)).toBe(true)
  })
})

// ── parseSelfEntity matches fixtures ─────────────────────────────────────────

describe('NetworkProtocol.parseSelfEntity matches fixtures', () => {
  it('parses self_entity_zero.hex correctly', () => {
    const fixture = loadSelfEntityFixture('self_entity_zero.hex')
    const view = new DataView(fixture.bytes.buffer, fixture.bytes.byteOffset, fixture.bytes.byteLength)
    
    const result = NetworkProtocol.parseSelfEntity(view)
    
    expect(result).not.toBeNull()
    expect(result?.entityId).toBe(0)
    expect(result?.tick).toBe(0)
  })

  it('parses self_entity_sample.hex correctly', () => {
    const fixture = loadSelfEntityFixture('self_entity_sample.hex')
    const view = new DataView(fixture.bytes.buffer, fixture.bytes.byteOffset, fixture.bytes.byteLength)
    
    const result = NetworkProtocol.parseSelfEntity(view)
    
    expect(result).not.toBeNull()
    expect(result?.entityId).toBe(42)
    expect(result?.tick).toBe(100)
  })

  it('parses self_entity_max.hex correctly', () => {
    const fixture = loadSelfEntityFixture('self_entity_max.hex')
    const view = new DataView(fixture.bytes.buffer, fixture.bytes.byteOffset, fixture.bytes.byteLength)
    
    const result = NetworkProtocol.parseSelfEntity(view)
    
    expect(result).not.toBeNull()
    expect(result?.entityId).toBe(0xFFFFFFFF)
    expect(result?.tick).toBe(0xFFFFFFFF)
  })
})

// ── Fixture loader tests ─────────────────────────────────────────────────────

describe('FixtureLoader', () => {
  it('loadInputFixture parses input_forward correctly', () => {
    const result = loadInputFixture('input_forward.hex')
    
    expect(result.bytes.length).toBe(14)
    expect(result.inputType).toBe(InputType.MOVE)
    expect(result.buttons).toBe(InputButton.FORWARD)
    expect(result.yaw).toBe(0)
    expect(result.pitch).toBe(0)
  })

  it('loadInputFixture parses input_complex correctly', () => {
    const result = loadInputFixture('input_complex.hex')
    
    expect(result.bytes.length).toBe(14)
    expect(result.inputType).toBe(InputType.MOVE)
    expect(result.buttons).toBe(InputButton.FORWARD | InputButton.LEFT)
    expect(result.yaw).toBeCloseTo(3.14159, 5)
    expect(result.pitch).toBe(-0.5)
  })

  it('loadJoinFixture parses join_player correctly', () => {
    const result = loadJoinFixture('join_player.hex')
    
    expect(result.bytes.length).toBe(21)
    expect(result.entityType).toBe(EntityType.PLAYER)
  })

  it('loadJoinFixture parses join_ghost correctly', () => {
    const result = loadJoinFixture('join_ghost.hex')
    
    expect(result.bytes.length).toBe(21)
    expect(result.entityType).toBe(EntityType.GHOST_PLAYER)
  })

  it('loadSelfEntityFixture parses self_entity_sample correctly', () => {
    const result = loadSelfEntityFixture('self_entity_sample.hex')
    
    expect(result.bytes.length).toBe(13)
    expect(result.entityId).toBe(42)
    expect(result.tick).toBe(100)
  })

  it('throws on missing fixture file', () => {
    expect(() => loadHexFixture('nonexistent/nonexistent.hex')).toThrow()
  })

  it('formatBytes formats correctly', () => {
    const bytes = new Uint8Array([0, 15, 255, 16])
    expect(formatBytes(bytes)).toBe('00 0f ff 10')
  })

  it('bytesEqual returns true for identical arrays', () => {
    const a = new Uint8Array([1, 2, 3])
    const b = new Uint8Array([1, 2, 3])
    expect(bytesEqual(a, b)).toBe(true)
  })

  it('bytesEqual returns false for different arrays', () => {
    const a = new Uint8Array([1, 2, 3])
    const b = new Uint8Array([1, 2, 4])
    expect(bytesEqual(a, b)).toBe(false)
  })

  it('bytesEqual returns false for different lengths', () => {
    const a = new Uint8Array([1, 2, 3])
    const b = new Uint8Array([1, 2])
    expect(bytesEqual(a, b)).toBe(false)
  })
})

// ── Cross-platform serialization round-trip tests ───────────────────────────

describe('Cross-platform serialization round-trip', () => {
  it('client INPUT serialization matches server fixture parsing', () => {
    // Client creates a message
    const clientMsg = NetworkProtocol.serializeInput(
      InputType.MOVE,
      InputButton.FORWARD | InputButton.JUMP, 
      Math.PI / 2, 
      -0.25
    )
    
    // Verify it would match what server expects (structure check)
    const view = new DataView(clientMsg)
    expect(view.getUint8(0)).toBe(ClientMessageType.INPUT)
    expect(view.getUint16(1, true)).toBe(14)
    expect(view.getUint8(3)).toBe(InputType.MOVE)
    expect(view.getUint8(4)).toBe(InputButton.FORWARD | InputButton.JUMP)
    expect(view.getFloat32(5, true)).toBeCloseTo(Math.PI / 2, 5)
    expect(view.getFloat32(9, true)).toBeCloseTo(-0.25, 5)
  })

  it('server SELF_ENTITY fixture matches client parsing', () => {
    // Load the fixture that server would generate
    const fixture = loadSelfEntityFixture('self_entity_sample.hex')
    
    // Client parses it
    const view = new DataView(fixture.bytes.buffer, fixture.bytes.byteOffset, fixture.bytes.byteLength)
    const parsed = NetworkProtocol.parseSelfEntity(view)
    
    expect(parsed).not.toBeNull()
    expect(parsed?.entityId).toBe(42)
    expect(parsed?.tick).toBe(100)
  })
})
