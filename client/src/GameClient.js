// @ts-check
import * as THREE from 'three'
import { CHUNK_SIZE_X, CHUNK_SIZE_Z } from './types.js'
import { NetworkProtocol, ServerMessageType } from './NetworkProtocol.js'
import { Chunk } from './Chunk.js'
import { EntityRegistry } from './EntityRegistry.js'
import { lz4Decompress, BufReader } from './utils.js'
import { BaseEntity } from './entities/BaseEntity.js'

/** @typedef {import('./types.js').ChunkIdPacked} ChunkIdPacked */

/** @type {Record<number, string>} */
const MSG_TYPE_NAMES = Object.fromEntries(
  Object.entries(ServerMessageType).map(([k, v]) => [v, k])
)

/**
 * @class GameClient
 * @description Manages the WebSocket connection, chunk state, and Three.js meshes.
 *
 * All incoming binary messages follow the wire format:
 *   byte[0]         – ServerMessageType (uint8)
 *   byte[1:3]       – message size (uint16 LE)
 *   For chunk state messages (types 0-5):
 *     byte[3:11]    – ChunkId packed as little-endian int64
 *     byte[11:15]   – tick (uint32 LE)
 *   byte[15:]       – payload (voxel section + entity section)
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
    this.#entityRegistry.setScene(scene)
  }

  /**
   * Current server tick for entity prediction.
   * @returns {number}
   */
  get currentTick() { return this.#latestServerTick }

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

  /**
   * Update all entity animations. Call once per animation frame.
   * @param {number} dt  Delta time in seconds.
   */
  updateEntities(dt) {
    for (const entity of this.#entityRegistry.all()) {
      if (entity.updateAnimation) {
        entity.updateAnimation(dt)
      }
    }
  }

  /** Remove all chunk meshes and clear internal state. */
  clear() {
    // Destroy entity meshes first
    for (const entity of this.#entityRegistry.all()) {
      if (entity.destroy) {
        entity.destroy(this.#scene)
      }
    }
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
   * Dispatch one parsed message to the appropriate handler.
   * Minimum valid chunk state message size is 15 bytes:
   *   type(1) + size(2) + ChunkId(8) + tick(4) = 15 bytes
   * @param {DataView} view  View over exactly one message (byteOffset may be non-zero).
   */
  #dispatch(view) {
    // First check for SELF_ENTITY (doesn't have chunk header)
    if (view.byteLength >= 3) {
      const msgType = view.getUint8(0)
      
      if (msgType === ServerMessageType.SELF_ENTITY) {
        const selfData = NetworkProtocol.parseSelfEntity(view)
        if (selfData) {
          console.log('[GameClient] SELF_ENTITY received:', selfData)
          this.#selfEntityId = selfData.entityId
        }
        return
      }

      // For chunk state messages, parse the full chunk header
      if (msgType <= 5) {  // CHUNK_SNAPSHOT through CHUNK_TICK_DELTA_COMPRESSED
        const header = NetworkProtocol.parseChunkHeader(view)
        if (!header) return

        const { msgType: type, chunkId, messageTick, cx, cy, cz } = header
        if (messageTick > this.#latestServerTick) this.#latestServerTick = messageTick



        let voxelCount = 0
        let entityCount = 0

        switch (type) {
          case ServerMessageType.CHUNK_SNAPSHOT_COMPRESSED:
            ({ voxelCount, entityCount } = this.#applySnapshot(view, chunkId, messageTick))
            break
          case ServerMessageType.CHUNK_SNAPSHOT_DELTA:
          case ServerMessageType.CHUNK_TICK_DELTA:
            ({ voxelCount, entityCount } = this.#applyVoxelDelta(view, false, chunkId, messageTick))
            break
          case ServerMessageType.CHUNK_SNAPSHOT_DELTA_COMPRESSED:
          case ServerMessageType.CHUNK_TICK_DELTA_COMPRESSED:
            ({ voxelCount, entityCount } = this.#applyVoxelDelta(view, true, chunkId, messageTick))
            break
        }

        console.debug('[GameClient] rx', MSG_TYPE_NAMES[type] ?? type,
          `chunk(${cx},${cy},${cz})`, view.byteLength + 'B',
          `voxels=${voxelCount}`, `entities=${entityCount}`)
      }
    }
  }

  /**
   * Apply a CHUNK_SNAPSHOT_COMPRESSED message.
   * @param {DataView} view
   * @param {ChunkIdPacked} chunkId
   * @param {number} messageTick
   * @returns {{voxelCount: number, entityCount: number}}
   */
  #applySnapshot(view, chunkId, messageTick) {
    const raw = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
    let off = 15  // skip type(1) + size(2) + chunkId(8) + tick(4)

    const flags = view.getUint8(off++)
    const cvs   = view.getInt32(off, true); off += 4

    // Get or create chunk and set voxels
    const chunk = this.#getOrCreateChunk(chunkId)
    const decompressedVoxels = lz4Decompress(raw.subarray(off, off + cvs), chunk.voxels.length)
    chunk.setVoxels(decompressedVoxels)
    off += cvs

    // Entity section
    const ess = view.getInt32(off, true); off += 4
    const entityCount = this.#entityRegistry.applySnapshotEntities(chunkId, view, off, ess, messageTick)
    off += ess

    chunk.dirty = true
    return { voxelCount: decompressedVoxels.length, entityCount }
  }

  /**
   * Apply a delta message (snapshot delta or tick delta).
   * @param {DataView} view
   * @param {boolean} compressed
   * @param {ChunkIdPacked} chunkId
   * @param {number} messageTick
   * @returns {{voxelCount: number, entityCount: number}}
   */
  #applyVoxelDelta(view, compressed, chunkId, messageTick) {
    const raw = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
    let payload, pOff = 0

    if (compressed) {
      const uncompSize = view.getInt32(15, true)
      payload = lz4Decompress(raw.subarray(19), uncompSize)
    } else {
      payload = raw
      pOff    = 15  // skip header: type(1) + size(2) + chunkId(8) + tick(4)
    }

    const pView = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
    const voxelCount = pView.getInt32(pOff, true); pOff += 4

    // Apply voxel deltas
    const chunk = this.#getOrCreateChunk(chunkId)
    for (let i = 0; i < voxelCount; i++) {
      const vidPacked = pView.getUint16(pOff, true); pOff += 2
      const vtype     = pView.getUint8(pOff++)
      const vy = (vidPacked >> 10) & 0x1f
      const vx = (vidPacked >>  5) & 0x1f
      const vz =  vidPacked        & 0x1f
      chunk.setVoxel(vx, vy, vz, vtype)
    }

    // Entity section (present in real server deltas; guard against test-only messages)
    let entityCount = 0
    if (pOff + 4 <= pView.byteLength) {
      const reader = new BufReader(pView, pOff)
      entityCount = this.#entityRegistry.applyDeltaEntities(chunkId, reader, messageTick)
    }

    chunk.dirty = true
    return { voxelCount, entityCount }
  }
}
