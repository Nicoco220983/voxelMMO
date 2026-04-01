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
 * Entity delta sub-type, single value per entity record in a delta.
 * @readonly
 * @enum {number}
 */
export const DeltaType = Object.freeze({
  CREATE_ENTITY:       0,  // Entity appears in this chunk
  UPDATE_ENTITY:       1,  // Entity already known; only dirty components present
  DELETE_ENTITY:       2,  // Entity removed from this chunk
  CHUNK_CHANGE_ENTITY: 3,  // Entity moved to different chunk
})

/**
 * First byte of every client → server binary WebSocket frame.
 * @readonly
 * @enum {number}
 */
export const ClientMessageType = Object.freeze({
  INPUT: 0,  // inputType uint8 + payload (variable size)
  JOIN:  1,  // EntityType uint8 = 5 bytes
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
 * Input type - determines how the server interprets the input.
 * @readonly
 * @enum {number}
 */
export const InputType = Object.freeze({
  MOVE: 0,               // Movement input: buttons(1) + yaw(4) + pitch(4) = 10 bytes payload
  VOXEL_DESTROY: 1,      // Voxel destroy: vx(4) + vy(4) + vz(4) = 12 bytes payload
  VOXEL_CREATE: 2,       // Voxel create: vx(4) + vy(4) + vz(4) + voxelType(1) = 13 bytes payload
  BULK_VOXEL_DESTROY: 3, // Bulk voxel destroy: startX(4)+Y(4)+Z(4) + endX(4)+Y(4)+Z(4) = 24 bytes payload
  BULK_VOXEL_CREATE: 4,  // Bulk voxel create: startX(4)+Y(4)+Z(4) + endX(4)+Y(4)+Z(4) + voxelType(1) = 25 bytes payload
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
   * Serialize an INPUT frame.
   * Dispatches to the appropriate serializer based on inputType.
   * 
   * For MOVE:      serializeInputMove(buttons, yaw, pitch)
   * For VOXEL_DESTROY: serializeInputVoxelDestroy(vx, vy, vz)
   * For VOXEL_CREATE: serializeInputVoxelCreate(vx, vy, vz, voxelType)
   * 
   * @param {number} inputType  InputType value.
   * @param {...*} args         Arguments depending on inputType.
   * @returns {ArrayBuffer}
   */
  static serializeInput(inputType, ...args) {
    switch (inputType) {
      case InputType.MOVE:
        return this.serializeInputMove(args[0], args[1], args[2])
      case InputType.VOXEL_DESTROY:
        return this.serializeInputVoxelDestroy(args[0], args[1], args[2])
      case InputType.VOXEL_CREATE:
        return this.serializeInputVoxelCreate(args[0], args[1], args[2], args[3])
      default:
        throw new Error(`Unknown inputType: ${inputType}`)
    }
  }

  /**
   * Serialize a MOVE input frame (14 bytes).
   * Wire: type(1) + size(2) + inputType(1) + buttons(1) + yaw float32LE(4) + pitch float32LE(4).
   * @param {number} buttons  InputButton bitmask.
   * @param {number} yaw      Yaw angle in radians.
   * @param {number} pitch    Pitch angle in radians.
   * @returns {ArrayBuffer}
   */
  static serializeInputMove(buttons, yaw, pitch) {
    const buf = new ArrayBuffer(14)
    const v   = new DataView(buf)
    v.setUint8(0,   ClientMessageType.INPUT)
    v.setUint16(1,  14, true)  // size
    v.setUint8(3,   InputType.MOVE)
    v.setUint8(4,   buttons)
    v.setFloat32(5, yaw,   true)
    v.setFloat32(9, pitch, true)
    return buf
  }

  /**
   * Serialize a VOXEL_DESTROY input frame (16 bytes).
   * Wire: type(1) + size(2) + inputType(1) + vx int32LE(4) + vy int32LE(4) + vz int32LE(4).
   * @param {number} vx  World voxel X coordinate.
   * @param {number} vy  World voxel Y coordinate.
   * @param {number} vz  World voxel Z coordinate.
   * @returns {ArrayBuffer}
   */
  static serializeInputVoxelDestroy(vx, vy, vz) {
    const buf = new ArrayBuffer(16)
    const v   = new DataView(buf)
    v.setUint8(0,   ClientMessageType.INPUT)
    v.setUint16(1,  16, true)  // size
    v.setUint8(3,   InputType.VOXEL_DESTROY)
    v.setInt32(4,   vx, true)
    v.setInt32(8,   vy, true)
    v.setInt32(12,  vz, true)
    return buf
  }

  /**
   * Serialize a VOXEL_CREATE input frame (17 bytes).
   * Wire: type(1) + size(2) + inputType(1) + vx int32LE(4) + vy int32LE(4) + vz int32LE(4) + voxelType(1).
   * @param {number} vx  World voxel X coordinate.
   * @param {number} vy  World voxel Y coordinate.
   * @param {number} vz  World voxel Z coordinate.
   * @param {number} voxelType  Voxel type to create.
   * @returns {ArrayBuffer}
   */
  static serializeInputVoxelCreate(vx, vy, vz, voxelType) {
    const buf = new ArrayBuffer(17)
    const v   = new DataView(buf)
    v.setUint8(0,   ClientMessageType.INPUT)
    v.setUint16(1,  17, true)  // size
    v.setUint8(3,   InputType.VOXEL_CREATE)
    v.setInt32(4,   vx, true)
    v.setInt32(8,   vy, true)
    v.setInt32(12,  vz, true)
    v.setUint8(16,  voxelType)
    return buf
  }

  /**
   * Serialize a BULK_VOXEL_DESTROY input frame (28 bytes).
   * Wire: type(1) + size(2) + inputType(1) + startX(4) + startY(4) + startZ(4) + endX(4) + endY(4) + endZ(4).
   * @param {number} startX  Start voxel X coordinate.
   * @param {number} startY  Start voxel Y coordinate.
   * @param {number} startZ  Start voxel Z coordinate.
   * @param {number} endX    End voxel X coordinate.
   * @param {number} endY    End voxel Y coordinate.
   * @param {number} endZ    End voxel Z coordinate.
   * @returns {ArrayBuffer}
   */
  static serializeInputBulkVoxelDestroy(startX, startY, startZ, endX, endY, endZ) {
    const buf = new ArrayBuffer(28)
    const v   = new DataView(buf)
    v.setUint8(0,   ClientMessageType.INPUT)
    v.setUint16(1,  28, true)  // size
    v.setUint8(3,   InputType.BULK_VOXEL_DESTROY)
    v.setInt32(4,   startX, true)
    v.setInt32(8,   startY, true)
    v.setInt32(12,  startZ, true)
    v.setInt32(16,  endX, true)
    v.setInt32(20,  endY, true)
    v.setInt32(24,  endZ, true)
    return buf
  }

  /**
   * Serialize a BULK_VOXEL_CREATE input frame (29 bytes).
   * Wire: type(1) + size(2) + inputType(1) + startX(4) + startY(4) + startZ(4) + endX(4) + endY(4) + endZ(4) + voxelType(1).
   * @param {number} startX    Start voxel X coordinate.
   * @param {number} startY    Start voxel Y coordinate.
   * @param {number} startZ    Start voxel Z coordinate.
   * @param {number} endX      End voxel X coordinate.
   * @param {number} endY      End voxel Y coordinate.
   * @param {number} endZ      End voxel Z coordinate.
   * @param {number} voxelType Voxel type to create.
   * @returns {ArrayBuffer}
   */
  static serializeInputBulkVoxelCreate(startX, startY, startZ, endX, endY, endZ, voxelType) {
    const buf = new ArrayBuffer(29)
    const v   = new DataView(buf)
    v.setUint8(0,   ClientMessageType.INPUT)
    v.setUint16(1,  29, true)  // size
    v.setUint8(3,   InputType.BULK_VOXEL_CREATE)
    v.setInt32(4,   startX, true)
    v.setInt32(8,   startY, true)
    v.setInt32(12,  startZ, true)
    v.setInt32(16,  endX, true)
    v.setInt32(20,  endY, true)
    v.setInt32(24,  endZ, true)
    v.setUint8(28,  voxelType)
    return buf
  }

  /**
   * Serialize a JOIN frame (21 bytes).
   * Wire: type(1) + size(2) + EntityType uint8(1) + sessionToken(16).
   * @param {number} entityType  EntityType value.
   * @param {Uint8Array} sessionToken  16-byte session token for entity recovery.
   * @returns {ArrayBuffer}
   */
  static serializeJoin(entityType, sessionToken) {
    const buf = new ArrayBuffer(21)
    const v   = new DataView(buf)
    v.setUint8(0,  ClientMessageType.JOIN)
    v.setUint16(1, 21, true)  // size
    v.setUint8(3,  entityType)
    // Copy session token (16 bytes)
    const tokenArray = new Uint8Array(buf, 4, 16)
    tokenArray.set(sessionToken)
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
