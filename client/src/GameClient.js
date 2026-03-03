// @ts-check
import * as THREE from 'three'
import { ChunkMessageType, ClientMessageType, CHUNK_SIZE_X, CHUNK_SIZE_Z } from './types.js'
import { Chunk } from './Chunk.js'

/** @typedef {import('./entities/BaseEntity.js').BaseEntity} BaseEntity */

/** @typedef {import('./types.js').ChunkIdPacked} ChunkIdPacked */

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
 * @description Manages the WebSocket connection, chunk state, and Three.js meshes.
 *
 * All incoming binary messages follow the chunk-state wire format:
 *   byte[0]    – ChunkMessageType
 *   byte[1:9]  – ChunkId packed as little-endian int64
 *   byte[9:]   – payload (voxel section + entity section)
 */
export class GameClient {
  /** @type {WebSocket|null} */
  #socket = null

  /** @type {string} */
  #url

  /** @type {THREE.Scene} */
  #scene

  /** @type {Map<ChunkIdPacked, Chunk>} */
  #chunks = new Map()

  /** @type {number} Server tick from the most-recently received message. */
  #latestServerTick = 0

  /** @type {bigint|null} ChunkId of the chunk containing the local player entity. */
  #selfChunkId = null

  /** @type {number|null} ChunkEntityId of the local player entity. */
  #selfEntityId = null

  /** @returns {number} */
  get latestServerTick() { return this.#latestServerTick }

  /**
   * Returns the local player's own entity as tracked by the server, or null until
   * a SELF_ENTITY message has been received and the entity's chunk snapshot is loaded.
   * @returns {BaseEntity|null}
   */
  get selfEntity() {
    if (this.#selfChunkId === null || this.#selfEntityId === null) return null
    return this.#chunks.get(this.#selfChunkId)?.entities.get(this.#selfEntityId) ?? null
  }

  /**
   * @param {string}      url    Full WebSocket URL, e.g. "ws://localhost:8080".
   * @param {THREE.Scene} scene  The Three.js scene to add/remove chunk meshes into.
   */
  constructor(url, scene) {
    this.#url   = url
    this.#scene = scene
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
   * Send a raw binary player-input message to the server.
   * @param {ArrayBuffer} data  Serialised input bytes.
   */
  sendInput(data) {
    if (this.#socket?.readyState === WebSocket.OPEN) {
      this.#socket.send(data)
    }
  }

  /**
   * Send a JOIN message declaring the entity type for this client.
   * Must be called once after connect(), before sending velocity input.
   * @param {number} entityType  EntityType value (e.g. EntityType.GHOST_PLAYER).
   */
  sendJoin(entityType) {
    const buf = new ArrayBuffer(2)
    const v   = new DataView(buf)
    v.setUint8(0, ClientMessageType.JOIN)
    v.setUint8(1, entityType)
    this.#socket?.send(buf)
  }

  /** Close the WebSocket connection. */
  disconnect() {
    this.#socket?.close()
    this.#socket = null
  }

  /**
   * Iterate all entities across all known chunks.
   * Each entry is { chunkId, entity } — use the composite key for stable mesh tracking.
   * @returns {IterableIterator<{chunkId: bigint, entity: BaseEntity}>}
   */
  * allEntities() {
    for (const [chunkId, chunk] of this.#chunks) {
      for (const entity of chunk.entities.values()) {
        yield { chunkId, entity }
      }
    }
  }

  /**
   * Rebuild Three.js meshes for all chunks that received voxel changes since
   * the last call. Call once per animation frame.
   */
  rebuildDirtyChunks() {
    let built = 0
    for (const chunk of this.#chunks.values()) {
      if (chunk.dirty) { chunk.rebuildMesh(this.#scene); if (++built >= 2) break }
    }
  }

  /** Remove all chunk meshes and clear internal state. */
  clear() {
    for (const chunk of this.#chunks.values()) {
      chunk.dispose(this.#scene)
    }
    this.#chunks.clear()
  }

  /**
   * Dispose and unload chunks that are beyond maxRadius chunks from the player.
   * @param {number} playerX
   * @param {number} playerZ
   * @param {number} [maxRadius=10]
   */
  pruneDistantChunks(playerX, playerZ, maxRadius = 10) {
    const pcx = Math.floor(playerX / CHUNK_SIZE_X)
    const pcz = Math.floor(playerZ / CHUNK_SIZE_Z)
    for (const [chunkId, chunk] of this.#chunks) {
      const cx = Number(BigInt.asIntN(29, chunkId >> 29n))
      const cz = Number(BigInt.asIntN(29, chunkId))
      if (Math.abs(cx - pcx) > maxRadius || Math.abs(cz - pcz) > maxRadius) {
        chunk.dispose(this.#scene)
        this.#chunks.delete(chunkId)
      }
    }
  }

  // ── private ──────────────────────────────────────────────────────────────

  /**
   * @param {ChunkIdPacked} chunkId
   * @returns {Chunk}
   */
  #getOrCreateChunk(chunkId) {
    let chunk = this.#chunks.get(chunkId)
    if (!chunk) {
      chunk = new Chunk(chunkId)
      this.#chunks.set(chunkId, chunk)
    }
    return chunk
  }

  /**
   * Parse an incoming WebSocket frame as a batch of length-prefixed messages.
   * Batch wire format: repeated [ uint32 msgLen (LE) | msgLen bytes ]
   * @param {ArrayBuffer} buf
   */
  #handleMessage(buf) {
    const view = new DataView(buf)
    let off = 0
    while (off + 4 <= buf.byteLength) {
      const len = view.getUint32(off, true); off += 4
      if (off + len > buf.byteLength) break
      this.#dispatch(new DataView(buf, off, len))
      off += len
    }
  }

  /**
   * Dispatch one parsed message to the appropriate chunk.
   * Minimum valid message size is 13 bytes: type(1) + ChunkId(8) + tick(4).
   * @param {DataView} view  View over exactly one message (byteOffset may be non-zero).
   */
  #dispatch(view) {
    if (view.byteLength < 13) return

    const msgType     = view.getUint8(0)
    const chunkId     = view.getBigInt64(1, /* littleEndian */ true)
    const messageTick = view.getUint32(9, /* littleEndian */ true)
    if (messageTick > this.#latestServerTick) this.#latestServerTick = messageTick

    const cy = Number(BigInt.asIntN(6,  chunkId >> 58n))
    const cx = Number(BigInt.asIntN(29, chunkId >> 29n))
    const cz = Number(BigInt.asIntN(29, chunkId))
    console.debug('[GameClient] rx', MSG_TYPE_NAMES[msgType] ?? msgType,
      `chunk(${cx},${cy},${cz})`, view.byteLength + 'B')

    switch (msgType) {
      case ChunkMessageType.SNAPSHOT_COMPRESSED:
        this.#getOrCreateChunk(chunkId).applySnapshot(view, messageTick)
        break
      case ChunkMessageType.SNAPSHOT_DELTA:
      case ChunkMessageType.TICK_DELTA:
        this.#chunks.get(chunkId)?.applyVoxelDelta(view, false, messageTick)
        break
      case ChunkMessageType.SNAPSHOT_DELTA_COMPRESSED:
      case ChunkMessageType.TICK_DELTA_COMPRESSED:
        this.#chunks.get(chunkId)?.applyVoxelDelta(view, true, messageTick)
        break
      case ChunkMessageType.SELF_ENTITY:
        if (view.byteLength >= 15) {
          this.#selfChunkId  = chunkId
          this.#selfEntityId = view.getUint16(13, true)
        }
        break
    }
  }
}
