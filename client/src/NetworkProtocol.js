// @ts-check
import { getChunkPos } from './types.js'

/**
 * Server message types - first byte of every server → client message.
 * All messages start with [type(1)][size(2)] header (3 bytes).
 * Chunk state messages (0-5) are followed by [chunk_id(8)][tick(4)] (12 bytes).
 * @readonly
 * @enum {number}
 */
export const ServerMessageType = Object.freeze({
  CHUNK_SNAPSHOT:                  0,  // Full chunk state (voxels + entities)
  CHUNK_SNAPSHOT_COMPRESSED:       1,  // LZ4-compressed snapshot
  CHUNK_SNAPSHOT_DELTA:            2,  // Delta since last snapshot
  CHUNK_SNAPSHOT_DELTA_COMPRESSED: 3,  // LZ4-compressed snapshot delta
  CHUNK_TICK_DELTA:                4,  // Per-tick delta
  CHUNK_TICK_DELTA_COMPRESSED:     5,  // LZ4-compressed tick delta
  SELF_ENTITY:                     6,  // Self-identification with GlobalEntityId
})

/**
 * Entity delta sub-type, first byte of each entity record in a delta.
 * @readonly
 * @enum {number}
 */
export const DeltaType = Object.freeze({
  CREATE_ENTITY:       1 << 0,  // Entity appears in this chunk (bit 0)
  UPDATE_ENTITY:       1 << 1,  // Entity component updates (bit 1)
  DELETE_ENTITY:       1 << 2,  // Entity removed from this chunk (bit 2)
  CHUNK_CHANGE_ENTITY: 1 << 3,  // Entity moved to different chunk (bit 3)
})

/**
 * Check if delta type includes CREATE_ENTITY flag.
 * @param {number} deltaType - The delta type byte.
 * @returns {boolean}
 */
export function isCreateDelta(deltaType) {
  return (deltaType & DeltaType.CREATE_ENTITY) !== 0
}

/**
 * Check if delta type includes UPDATE_ENTITY flag.
 * @param {number} deltaType - The delta type byte.
 * @returns {boolean}
 */
export function isUpdateDelta(deltaType) {
  return (deltaType & DeltaType.UPDATE_ENTITY) !== 0
}

/**
 * Check if delta type includes DELETE_ENTITY flag.
 * @param {number} deltaType - The delta type byte.
 * @returns {boolean}
 */
export function isDeleteDelta(deltaType) {
  return (deltaType & DeltaType.DELETE_ENTITY) !== 0
}

/**
 * Check if delta type includes CHUNK_CHANGE_ENTITY flag.
 * @param {number} deltaType - The delta type byte.
 * @returns {boolean}
 */
export function isChunkChangeDelta(deltaType) {
  return (deltaType & DeltaType.CHUNK_CHANGE_ENTITY) !== 0
}

/**
 * First byte of every client → server binary WebSocket frame.
 * @readonly
 * @enum {number}
 */
export const ClientMessageType = Object.freeze({
  INPUT: 0,  // buttons uint8 + yaw float32 + pitch float32
  JOIN:  1,  // EntityType uint8
})

/**
 * Bitmask flags for the INPUT message buttons field.
 * @readonly
 * @enum {number}
 */
export const InputButton = Object.freeze({
  FORWARD:  1 << 0,
  BACKWARD: 1 << 1,
  LEFT:     1 << 2,
  RIGHT:    1 << 3,
  JUMP:     1 << 4,
  DESCEND:  1 << 5,
})

/**
 * Header sizes (in bytes).
 * @readonly
 */
export const HeaderSize = Object.freeze({
  MESSAGE: 3,   // [type(1)][size(2)]
  CHUNK:   15,  // [type(1)][size(2)][chunk_id(8)][tick(4)]
})

/**
 * Serialization and deserialization helpers for the client↔server wire protocol.
 *
 * All methods are stateless and side-effect-free — they only read/write bytes.
 * Transport (WebSocket send/receive) and game logic remain in GameClient / main.js.
 */
export class NetworkProtocol {
  // ── Client → Server (serialization) ────────────────────────────────────────

  /**
   * Serialize an INPUT frame (13 bytes).
   * Wire: type(1) + size(2) + buttons uint8(1) + yaw float32LE(4) + pitch float32LE(4).
   * @param {number} buttons  InputButton bitmask.
   * @param {number} yaw      Yaw angle in radians.
   * @param {number} pitch    Pitch angle in radians.
   * @returns {ArrayBuffer}
   */
  static serializeInput(buttons, yaw, pitch) {
    const buf = new ArrayBuffer(13)
    const v   = new DataView(buf)
    v.setUint8(0,   ClientMessageType.INPUT)
    v.setUint16(1,  13, true)  // size
    v.setUint8(3,   buttons)
    v.setFloat32(4, yaw,   true)
    v.setFloat32(8, pitch, true)
    return buf
  }

  /**
   * Serialize a JOIN frame (5 bytes).
   * Wire: type(1) + size(2) + EntityType uint8(1).
   * @param {number} entityType  EntityType value.
   * @returns {ArrayBuffer}
   */
  static serializeJoin(entityType) {
    const buf = new ArrayBuffer(5)
    const v   = new DataView(buf)
    v.setUint8(0,  ClientMessageType.JOIN)
    v.setUint16(1, 5, true)  // size
    v.setUint8(3,  entityType)
    return buf
  }

  // ── Server → Client (deserialization) ──────────────────────────────────────

  /**
   * Parse a batch of concatenated messages from a WebSocket frame.
   * Batch wire format: direct concatenation of messages. Each message has a
   * [type(1)][size(2)] header where size includes the header itself.
   * @param {ArrayBuffer} buf
   * @returns {Generator<DataView, void, unknown>}
   */
  static * parseBatch(buf) {
    const view = new DataView(buf)
    let off = 0
    while (off + 3 <= buf.byteLength) {
      // Read size from header (bytes 1-2, little-endian)
      const msgSize = view.getUint16(off + 1, true)
      if (off + msgSize > buf.byteLength) break
      yield new DataView(buf, off, msgSize)
      off += msgSize
    }
  }

  /**
   * Parse the universal 3-byte message header.
   * Wire: type(1) + size uint16LE(2).
   * @param {DataView} view
   * @returns {{ msgType: number, size: number } | null}
   *   null if the message is shorter than 3 bytes.
   */
  static parseHeader(view) {
    if (view.byteLength < 3) return null
    const msgType = view.getUint8(0)
    const size    = view.getUint16(1, true)
    return { msgType, size }
  }

  /**
   * Parse the 15-byte chunk state message header.
   * Wire: type(1) + size(2) + ChunkId int64LE(8) + tick uint32LE(4).
   * @param {DataView} view
   * @returns {{ msgType: number, size: number, chunkId: import('./types.js').ChunkId, messageTick: number, cx: number, cy: number, cz: number } | null}
   *   null if the message is shorter than 15 bytes.
   */
  static parseChunkHeader(view) {
    if (view.byteLength < 15) return null
    const msgType     = view.getUint8(0)
    const size        = view.getUint16(1, true)
    const chunkIdPacked = view.getBigInt64(3, true)
    const messageTick = view.getUint32(11, true)
    const { cx, cy, cz } = getChunkPos(chunkIdPacked)
    return { msgType, size, chunkId: chunkIdPacked, messageTick, cx, cy, cz }
  }

  /**
   * Parse a SELF_ENTITY message.
   * Wire: type(1) + size(2) + GlobalEntityId uint32LE(4) + tick uint32LE(4) + reserved uint32LE(4).
   * @param {DataView} view
   * @returns {{ entityId: number, tick: number } | null}
   *   null if the message is not a valid SELF_ENTITY.
   */
  static parseSelfEntity(view) {
    if (view.byteLength < 13) return null
    const msgType = view.getUint8(0)
    if (msgType !== ServerMessageType.SELF_ENTITY) return null
    const entityId = view.getUint32(3, true)
    const tick     = view.getUint32(7, true)
    // bytes 11-14: reserved for future use, ignored
    return { entityId, tick }
  }
}
