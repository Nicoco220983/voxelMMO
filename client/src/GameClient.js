// @ts-check
import { ChunkMessageType } from './types.js'

/** @type {Record<number, string>} */
const MSG_TYPE_NAMES = Object.fromEntries(
  Object.entries(ChunkMessageType).map(([k, v]) => [v, k])
)

/**
 * Callback invoked for every parsed chunk-state message.
 * @callback ChunkMessageHandler
 * @param {number}        type     ChunkMessageType value.
 * @param {bigint}        chunkId  Packed ChunkId bigint.
 * @param {DataView}      view     View over the full raw buffer.
 */

/**
 * @class GameClient
 * @description Manages the WebSocket connection to the gateway engine.
 *
 * All incoming binary messages follow the chunk-state wire format:
 *   byte[0]    – ChunkMessageType
 *   byte[1:9]  – ChunkId packed as little-endian int64
 *   byte[9:]   – payload (voxel section + entity section)
 *
 * Outgoing messages carry raw player input (binary format defined by game design).
 */
export class GameClient {
  /** @type {WebSocket|null} */
  #socket = null

  /** @type {ChunkMessageHandler|null} */
  #chunkMessageHandler = null

  /** @type {string} */
  #url

  /**
   * @param {string} url  Full WebSocket URL, e.g. "ws://localhost:8080".
   */
  constructor(url) {
    this.#url = url
  }

  /**
   * Open the WebSocket connection.
   * @returns {Promise<void>} Resolves when the connection is established.
   */
  connect() {
    return new Promise((resolve, reject) => {
      this.#socket = new WebSocket(this.#url)
      this.#socket.binaryType = 'arraybuffer'

      this.#socket.addEventListener('open', () => {
        console.info('[GameClient] Connected to', this.#url)
        resolve(undefined)
      })

      this.#socket.addEventListener('error', (e) => {
        console.error('[GameClient] WebSocket error', e)
        reject(e)
      })

      this.#socket.addEventListener('message', (/** @type {MessageEvent<ArrayBuffer>} */ ev) => {
        this.#handleMessage(ev.data)
      })

      this.#socket.addEventListener('close', (ev) => {
        console.info('[GameClient] Disconnected (code=%d)', ev.code)
      })
    })
  }

  /**
   * Register the handler that receives all parsed chunk messages.
   * @param {ChunkMessageHandler} handler
   */
  onChunkMessage(handler) {
    this.#chunkMessageHandler = handler
  }

  /**
   * Send a raw binary player-input message to the server.
   * @param {ArrayBuffer} data  Serialised input bytes.
   */
  sendInput(data) {
    if (this.#socket?.readyState === WebSocket.OPEN) {
      this.#socket.send(data)
    }
  }

  /** Close the WebSocket connection. */
  disconnect() {
    this.#socket?.close()
    this.#socket = null
  }

  // ── private ──────────────────────────────────────────────────────────────

  /**
   * Parse an incoming binary message and dispatch to the registered handler.
   * Minimum valid message size is 9 bytes: type(1) + ChunkId(8).
   * @param {ArrayBuffer} buf
   */
  #handleMessage(buf) {
    if (buf.byteLength < 9) return

    const view    = new DataView(buf)
    const msgType = view.getUint8(0)
    // ChunkId is a little-endian signed 64-bit integer
    const chunkId = view.getBigInt64(1, /* littleEndian */ true)

    const cy = Number(BigInt.asIntN(6,  chunkId >> 58n))
    const cx = Number(BigInt.asIntN(29, chunkId >> 29n))
    const cz = Number(BigInt.asIntN(29, chunkId))
    console.debug('[GameClient] rx', MSG_TYPE_NAMES[msgType] ?? msgType,
      `chunk(${cx},${cy},${cz})`, buf.byteLength + 'B')

    this.#chunkMessageHandler?.(msgType, chunkId, view)
  }
}
