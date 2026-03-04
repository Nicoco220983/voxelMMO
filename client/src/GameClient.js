// @ts-check
import * as THREE from 'three'
import { ChunkMessageType, CHUNK_SIZE_X, CHUNK_SIZE_Z } from './types.js'
import { NetworkProtocol } from './NetworkProtocol.js'
import { Chunk } from './Chunk.js'
import { EntityRegistry } from './EntityRegistry.js'
import { lz4Decompress, BufReader } from './utils.js'
import { BaseEntity } from './entities/BaseEntity.js'

/** @typedef {import('./types.js').ChunkIdPacked} ChunkIdPacked */

/** @type {Record<number, string>} */
const MSG_TYPE_NAMES = Object.fromEntries(
  Object.entries(ChunkMessageType).map(([k, v]) => [v, k])
)

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

  /** @type {EntityRegistry} */
  #entityRegistry = new EntityRegistry()

  /** @type {number} Server tick from the most-recently received message. */
  #latestServerTick = 0

  /** @type {number|null} GlobalEntityId of the local player entity. */
  #selfEntityId = null

  /** @returns {number} */
  get latestServerTick() { return this.#latestServerTick }

  /**
   * Returns the local player's own entity as tracked by the server, or null until
   * the SELF_ENTITY message has been received.
   * @returns {BaseEntity|null}
   */
  get selfEntity() {
    if (this.#selfEntityId === null) return null
    return this.#entityRegistry.get(this.#selfEntityId) ?? null
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
    this.#socket?.send(NetworkProtocol.serializeJoin(entityType))
  }

  /** Close the WebSocket connection. */
  disconnect() {
    this.#socket?.close()
    this.#socket = null
  }

  /**
   * Iterate all entities across all known chunks.
   * Each entry is { chunkId, entity } — use the composite key for stable mesh tracking.
   * @returns {IterableIterator<{chunkId: ChunkIdPacked, entity: BaseEntity}>}
   */
  * allEntities() {
    for (const entity of this.#entityRegistry.all()) {
      if (entity.chunkId !== undefined) {
        yield { chunkId: entity.chunkId, entity }
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
    this.#entityRegistry.clear()
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
        this.#entityRegistry.removeChunk(chunkId)
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
    for (const msgView of NetworkProtocol.parseBatch(buf)) {
      this.#dispatch(msgView)
    }
  }

  /**
   * Dispatch one parsed message to the appropriate chunk.
   * Minimum valid message size is 13 bytes: type(1) + ChunkId(8) + tick(4).
   * @param {DataView} view  View over exactly one message (byteOffset may be non-zero).
   */
  #dispatch(view) {
    const header = NetworkProtocol.parseHeader(view)
    if (!header) return

    const { msgType, chunkId, messageTick, cx, cy, cz } = header
    if (messageTick > this.#latestServerTick) this.#latestServerTick = messageTick

    console.debug('[GameClient] rx', MSG_TYPE_NAMES[msgType] ?? msgType,
      `chunk(${cx},${cy},${cz})`, view.byteLength + 'B')

    switch (msgType) {
      case ChunkMessageType.SNAPSHOT_COMPRESSED:
        this.#applySnapshot(view, chunkId, messageTick)
        break
      case ChunkMessageType.SNAPSHOT_DELTA:
      case ChunkMessageType.TICK_DELTA:
        this.#applyVoxelDelta(view, false, chunkId, messageTick)
        break
      case ChunkMessageType.SNAPSHOT_DELTA_COMPRESSED:
      case ChunkMessageType.TICK_DELTA_COMPRESSED:
        this.#applyVoxelDelta(view, true, chunkId, messageTick)
        break
      case ChunkMessageType.SELF_ENTITY:
        if (view.byteLength >= 17) {
          this.#selfEntityId = view.getUint32(13, true)
        }
        break
    }
  }

  /**
   * Apply a SNAPSHOT_COMPRESSED message.
   * @param {DataView} view
   * @param {ChunkIdPacked} chunkId
   * @param {number} messageTick
   */
  #applySnapshot(view, chunkId, messageTick) {
    const raw = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
    let off = 13  // skip type(1) + chunkId(8) + tick(4)

    const flags = view.getUint8(off++)
    const cvs   = view.getInt32(off, true); off += 4

    // Get or create chunk and set voxels
    const chunk = this.#getOrCreateChunk(chunkId)
    chunk.setVoxels(lz4Decompress(raw.subarray(off, off + cvs), chunk.voxels.length))
    off += cvs

    // Entity section
    const ess = view.getInt32(off, true); off += 4
    this.#entityRegistry.applySnapshotEntities(chunkId, view, off, ess, messageTick)
    off += ess

    chunk.dirty = true
  }

  /**
   * Apply a delta message (snapshot delta or tick delta).
   * @param {DataView} view
   * @param {boolean} compressed
   * @param {ChunkIdPacked} chunkId
   * @param {number} messageTick
   */
  #applyVoxelDelta(view, compressed, chunkId, messageTick) {
    const raw = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
    let payload, pOff = 0

    if (compressed) {
      const uncompSize = view.getInt32(13, true)
      payload = lz4Decompress(raw.subarray(17), uncompSize)
    } else {
      payload = raw
      pOff    = 13
    }

    const pView = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
    const count = pView.getInt32(pOff, true); pOff += 4

    // Apply voxel deltas
    const chunk = this.#getOrCreateChunk(chunkId)
    for (let i = 0; i < count; i++) {
      const vidPacked = pView.getUint16(pOff, true); pOff += 2
      const vtype     = pView.getUint8(pOff++)
      const vy = (vidPacked >> 10) & 0x1f
      const vx = (vidPacked >>  5) & 0x1f
      const vz =  vidPacked        & 0x1f
      chunk.setVoxel(vx, vy, vz, vtype)
    }

    // Entity section (present in real server deltas; guard against test-only messages)
    if (pOff + 4 <= pView.byteLength) {
      const reader = new BufReader(pView, pOff)
      this.#entityRegistry.applyDeltaEntities(chunkId, reader, messageTick)
    }

    chunk.dirty = true
  }
}
