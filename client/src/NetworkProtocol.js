// @ts-check
import { ClientMessageType } from './types.js'

/**
 * Serialization and deserialization helpers for the client↔server wire protocol.
 *
 * All methods are stateless and side-effect-free — they only read/write bytes.
 * Transport (WebSocket send/receive) and game logic remain in GameClient / main.js.
 */
export class NetworkProtocol {
  // ── Client → Server (serialization) ────────────────────────────────────────

  /**
   * Serialize an INPUT frame (10 bytes).
   * Wire: type(1) + buttons uint8(1) + yaw float32LE(4) + pitch float32LE(4).
   * @param {number} buttons  InputButton bitmask.
   * @param {number} yaw      Yaw angle in radians.
   * @param {number} pitch    Pitch angle in radians.
   * @returns {ArrayBuffer}
   */
  static serializeInput(buttons, yaw, pitch) {
    const buf = new ArrayBuffer(10)
    const v   = new DataView(buf)
    v.setUint8(0,   ClientMessageType.INPUT)
    v.setUint8(1,   buttons)
    v.setFloat32(2, yaw,   true)
    v.setFloat32(6, pitch, true)
    return buf
  }

  /**
   * Serialize a JOIN frame (2 bytes).
   * Wire: type(1) + EntityType uint8(1).
   * @param {number} entityType  EntityType value.
   * @returns {ArrayBuffer}
   */
  static serializeJoin(entityType) {
    const buf = new ArrayBuffer(2)
    const v   = new DataView(buf)
    v.setUint8(0, ClientMessageType.JOIN)
    v.setUint8(1, entityType)
    return buf
  }

  // ── Server → Client (deserialization) ──────────────────────────────────────

  /**
   * Parse a batch of length-prefixed messages from a WebSocket frame.
   * Batch wire format: repeated [ uint32 msgLen (LE) | msgLen bytes ].
   * @param {ArrayBuffer} buf
   * @returns {Generator<DataView, void, unknown>}
   */
  static * parseBatch(buf) {
    const view = new DataView(buf)
    let off = 0
    while (off + 4 <= buf.byteLength) {
      const len = view.getUint32(off, true); off += 4
      if (off + len > buf.byteLength) break
      yield new DataView(buf, off, len)
      off += len
    }
  }

  /**
   * Parse the 13-byte header common to all chunk-state messages.
   * Wire: type(1) + ChunkId int64LE(8) + tick uint32LE(4).
   * @param {DataView} view
   * @returns {{ msgType: number, chunkId: bigint, messageTick: number, cx: number, cy: number, cz: number } | null}
   *   null if the message is shorter than 13 bytes.
   */
  static parseHeader(view) {
    if (view.byteLength < 13) return null
    const msgType     = view.getUint8(0)
    const chunkId     = view.getBigInt64(1, true)
    const messageTick = view.getUint32(9, true)
    const cy = Number(BigInt.asIntN(6,  chunkId >> 58n))
    const cx = Number(BigInt.asIntN(29, chunkId >> 29n))
    const cz = Number(BigInt.asIntN(29, chunkId))
    return { msgType, chunkId, messageTick, cx, cy, cz }
  }
}
