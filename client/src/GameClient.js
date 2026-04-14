// @ts-check
import * as THREE from 'three'
import { CHUNK_SIZE_X, CHUNK_SIZE_Z, getChunkPos } from './types.js'

/** @typedef {import('./types.js').SubVoxelCoord} SubVoxelCoord */
/** @typedef {import('./types.js').VoxelCoord} VoxelCoord */
/** @typedef {import('./types.js').ChunkCoord} ChunkCoord */
import { NetworkProtocol, ServerMessageType } from './NetworkProtocol.js'
import { Chunk } from './Chunk.js'
import { ChunkRegistry } from './ChunkRegistry.js'
import { EntityRegistry } from './EntityRegistry.js'
import { EntityDeserializer } from './EntityDeserializer.js'
import { lz4Decompress, BufReader } from './utils.js'
import { BaseEntity } from './entities/BaseEntity.js'
import { TICK_RATE } from './types.js'
import { PhysicsPredictionSystem } from './systems/PhysicsPredictionSystem.js'
import { ChunkMembershipSystem } from './systems/ChunkMembershipSystem.js'
import { onVoxelTexturesLoaded } from './VoxelTextures.js'

/** @typedef {import('./types.js').ChunkId} ChunkId */

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

  /** @type {ChunkRegistry} */
  #chunkRegistry = new ChunkRegistry()

  /** @type {EntityRegistry} */
  #entityRegistry = new EntityRegistry()

  /** @type {number} Server tick from the most-recently received message. */
  #latestServerTick = 0
  #latestServerTickTimeMs = Date.now()

  /** @type {Function|null} Callback when player dies */
  #onDeathCallback = null

  /** @type {Function|null} Callback when player takes damage */
  #onDamageCallback = null

  /**
   * Returns the local player's own entity as tracked by the server, or null until
   * the SELF_ENTITY message has been received.
   * @returns {BaseEntity|null}
   */
  get selfEntity() {
    const selfId = this.#entityRegistry.selfEntityId
    if (selfId === null) return null
    return this.#entityRegistry.get(selfId) ?? null
  }

  /**
   * Get the chunk registry for accessing voxel data.
   * @returns {ChunkRegistry}
   */
  get chunkRegistry() {
    return this.#chunkRegistry
  }

  /**
   * @param {string}      url    Full WebSocket URL, e.g. "ws://localhost:8080".
   * @param {THREE.Scene} scene  The Three.js scene to add/remove chunk meshes into.
   */
  constructor(url, scene, camera) {
    this.#url   = url
    this.#scene = scene
    this.#entityRegistry.setScene(scene)

    onVoxelTexturesLoaded(() => {
      for (const chunk of this.#chunkRegistry.values()) {
        chunk.dirty = true
      }
    })
  }

  /**
   * Send tool selection to server.
   * Called by Hotbar when user selects a tool.
   * @param {number} toolId
   */
  sendToolSelect(toolId) {
    if (this.#socket?.readyState === WebSocket.OPEN) {
      this.#socket.send(NetworkProtocol.serializeInputToolSelect(toolId))
    }
  }

  /**
   * Set callback for player death event.
   * Called when player's health reaches 0.
   * @param {Function} callback
   */
  onDeath(callback) {
    this.#onDeathCallback = callback
  }

  /**
   * Set callback for player damage event.
   * Called when player's health decreases from server update.
   * @param {Function} callback
   * @deprecated Use health.hasBeenDamaged() in render loop instead
   */
  onDamage(callback) {
    this.#onDamageCallback = callback
  }

  /**
   * Check if player is dead and trigger death callback.
   * Called during entity updates.
   * @private
   */
  #checkPlayerDeath() {
    const self = this.selfEntity
    if (!self) return

    // Check if health component exists and player is dead
    if (self.health && self.health.isDead && this.#onDeathCallback) {
      console.info('[GameClient] Player died, triggering onDeath callback')
      this.#onDeathCallback()
      this.#onDeathCallback = null  // Only trigger once
    }
  }

  /**
   * Current server tick for entity prediction.
   * @returns {number}
   */
  get tick() {
    return this.#latestServerTick + Math.floor((Date.now()-this.#latestServerTickTimeMs)/1000*TICK_RATE)
  }

  /**
   * Server tick with sub-tick fractional part for smooth interpolation.
   * Returns float where integer part = last server tick, fractional part = progress to next tick.
   * @returns {number} Float tick value (e.g., 150.6 = 60% of the way to tick 151)
   */
  get renderTick() {
    const elapsedMs = Date.now() - this.#latestServerTickTimeMs
    const fraction = (elapsedMs / 1000) * TICK_RATE
    return this.#latestServerTick + fraction
  }

  /**
   * @param {number}      serverTick
   */
  syncTick(serverTick) {
    if(serverTick <= this.#latestServerTick) return
    this.#latestServerTick = serverTick
    this.#latestServerTickTimeMs = Date.now()
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
   * @param {Uint8Array} sessionToken  16-byte session token for entity recovery.
   */
  sendJoin(entityType, sessionToken) {
    this.#socket?.send(NetworkProtocol.serializeJoin(entityType, sessionToken))
  }

  /**
   * Send a VOXEL_DESTROY input to the server.
   * @param {number} vx  World voxel X coordinate.
   * @param {number} vy  World voxel Y coordinate.
   * @param {number} vz  World voxel Z coordinate.
   */
  sendVoxelDestroy(vx, vy, vz) {
    if (this.#socket?.readyState === WebSocket.OPEN) {
      this.#socket.send(NetworkProtocol.serializeInputVoxelDestroy(vx, vy, vz))
    }
  }

  /**
   * Send a VOXEL_CREATE input to the server.
   * @param {number} vx  World voxel X coordinate.
   * @param {number} vy  World voxel Y coordinate.
   * @param {number} vz  World voxel Z coordinate.
   * @param {number} voxelType  Voxel type to create.
   */
  sendVoxelCreate(vx, vy, vz, voxelType) {
    if (this.#socket?.readyState === WebSocket.OPEN) {
      this.#socket.send(NetworkProtocol.serializeInputVoxelCreate(vx, vy, vz, voxelType))
    }
  }

  /** Close the WebSocket connection. */
  disconnect() {
    this.#socket?.close()
    this.#socket = null
  }

  /**
   * Iterate all entities across all known chunks.
   * Each entry is { chunkId, entity } — use the composite key for stable mesh tracking.
   * @returns {IterableIterator<{chunkId: ChunkId, entity: BaseEntity}>}
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
    for (const chunk of this.#chunkRegistry.values()) {
      if (chunk.dirty) { chunk.rebuildMesh(this.#scene) }
    }
  }

  /**
   * Update all entity predictions and animations. Call once per animation frame.
   * @param {number} dt  Delta time in seconds.
   */
  updateEntities(dt) {
    // Run physics prediction for all entities using sub-tick precision for smooth interpolation
    PhysicsPredictionSystem.update(this.#entityRegistry, this.renderTick)
    
    // Update chunk membership based on predicted positions
    ChunkMembershipSystem.update(this.#entityRegistry, this.#chunkRegistry)
    
    // Check if player died and trigger callback
    this.#checkPlayerDeath()
    
    // Update entity animations
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
    this.#chunkRegistry.clear(this.#scene)
    this.#entityRegistry.clear()
    this.#entityRegistry.setSelfEntityId(null)
  }

  /**
   * Dispose and unload chunks that are beyond maxRadius chunks from the player.
   * @param {VoxelCoord} playerX
   * @param {VoxelCoord} playerZ
   * @param {number} [maxRadius=10]
   */
  pruneDistantChunks(playerX, playerZ, maxRadius = 10) {
    const pcx = Math.floor(playerX / CHUNK_SIZE_X)
    const pcz = Math.floor(playerZ / CHUNK_SIZE_Z)
    for (const [chunkIdPacked, chunk] of this.#chunkRegistry.entries()) {
      const { cx, cz } = getChunkPos(chunkIdPacked)
      if (Math.abs(cx - pcx) > maxRadius || Math.abs(cz - pcz) > maxRadius) {
        // Remove entities belonging to this chunk
        for (const entityId of chunk.entities) {
          this.#entityRegistry.deleteEntity(this.#chunkRegistry, entityId)
        }
        this.#chunkRegistry.remove(chunkIdPacked, this.#scene)
      }
    }
  }

  // ── private ──────────────────────────────────────────────────────────────

  /**
   * @param {ChunkId} chunkId
   * @returns {Chunk}
   */
  #getOrCreateChunk(chunkId) {
    return this.#chunkRegistry.getOrCreate(chunkId)
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
          console.debug('[GameClient] SELF_ENTITY deserialized:', selfData)
          this.#entityRegistry.setSelfEntityId(selfData.entityId)
        } else {
          console.error('[GameClient] Failed to parse SELF_ENTITY message, byteLength:', view.byteLength)
        }
        return
      }

      // For chunk state messages, parse the full chunk header
      if (msgType <= 5) {  // CHUNK_SNAPSHOT through CHUNK_TICK_DELTA_COMPRESSED
        const header = NetworkProtocol.parseChunkHeader(view)
        if (!header) return

        const { msgType: type, chunkId, messageTick, cx, cy, cz } = header

        this.syncTick(messageTick)

        let voxelCount = 0
        let entityCount = 0

        switch (type) {
          case ServerMessageType.CHUNK_SNAPSHOT_COMPRESSED:
            ({ voxelCount, entityCount } = this.#applySnapshot(view, chunkId, messageTick))
            break
          case ServerMessageType.CHUNK_SNAPSHOT_DELTA:
            ({ voxelCount, entityCount } = this.#applySnapshotDelta(view, false, chunkId, messageTick))
            break
          case ServerMessageType.CHUNK_SNAPSHOT_DELTA_COMPRESSED:
            ({ voxelCount, entityCount } = this.#applySnapshotDelta(view, true, chunkId, messageTick))
            break
          case ServerMessageType.CHUNK_TICK_DELTA:
            ({ voxelCount, entityCount } = this.#applyTickDelta(view, false, chunkId, messageTick))
            break
          case ServerMessageType.CHUNK_TICK_DELTA_COMPRESSED:
            ({ voxelCount, entityCount } = this.#applyTickDelta(view, true, chunkId, messageTick))
            break
          default:
            console.error('[GameClient] Unknown chunk message type:', type)
        }

        console.debug('[GameClient] rx', MSG_TYPE_NAMES[type] ?? type,
          `chunk(${cx},${cy},${cz})`, view.byteLength + 'B',
          `voxels=${voxelCount}`, `entities=${entityCount}`)

        // Tool state is synced via chunk deltas (selfEntity.toolId)
      }
    }
  }

  /**
   * Get the current tool ID from the self entity (or null if not known).
   * @returns {number|null}
   */
  getSelfToolId() {
    return this.selfEntity?.toolId ?? null
  }

  /**
   * Apply a CHUNK_SNAPSHOT_COMPRESSED message.
   * @param {DataView} view
   * @param {ChunkId} chunkId
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
    const entityCount = EntityDeserializer.applySnapshotEntities(this.#entityRegistry, this.#chunkRegistry, chunkId, view, off, ess, messageTick)
    off += ess

    chunk.dirty = true
    return { voxelCount: decompressedVoxels.length, entityCount }
  }

  /**
   * Decompress message payload if needed and parse voxel deltas.
   * @param {DataView} view
   * @param {boolean} compressed
   * @param {ChunkId} chunkId
   * @returns {{chunk: Chunk, pView: DataView, pOff: number, voxelCount: number}}
   */
  #parseVoxelDeltas(view, compressed, chunkId) {
    const raw = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)
    let payload, pOff = 0

    if (compressed) {
      const uncompSize = view.getInt32(15, true)
      payload = lz4Decompress(raw.subarray(19), uncompSize)
    } else {
      payload = raw.subarray(15)  // skip header: type(1) + size(2) + chunkId(8) + tick(4)
    }

    const pView = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
    const voxelCount = pView.getInt32(pOff, true); pOff += 4

    const chunk = this.#getOrCreateChunk(chunkId)
    for (let i = 0; i < voxelCount; i++) {
      const vidPacked = pView.getUint16(pOff, true); pOff += 2
      const vtype     = pView.getUint8(pOff++)
      const vy = (vidPacked >> 10) & 0x1f
      const vx = (vidPacked >>  5) & 0x1f
      const vz =  vidPacked        & 0x1f
      chunk.setVoxel(vx, vy, vz, vtype)
    }

    return { chunk, pView, pOff, voxelCount }
  }

  /**
   * Apply a snapshot delta message (sends full entities like a snapshot).
   * @param {DataView} view
   * @param {boolean} compressed
   * @param {ChunkId} chunkId
   * @param {number} messageTick
   * @returns {{voxelCount: number, entityCount: number}}
   */
  #applySnapshotDelta(view, compressed, chunkId, messageTick) {
    const { chunk, pView, pOff: entityOff, voxelCount } = this.#parseVoxelDeltas(view, compressed, chunkId)

    // Entity section: full entities (like snapshot), not deltas
    // The entity section format matches snapshots:
    //   [entity_section_stored_size(4)] [entity_data]
    // where entity_data starts with entity_count(4) if uncompressed,
    // or [uncomp_size(4)][lz4_data] if compressed
    let entityCount = 0
    if (entityOff + 4 <= pView.byteLength) {
      // Read entity_section_stored_size (size of entity_data that follows)
      const ess = pView.getInt32(entityOff, true)
      const payloadOff = entityOff + 4
      if (ess > 0 && payloadOff + ess <= pView.byteLength) {
        // Get flags from message header (position 15) - indicates if entity section is compressed
        const flags = view.getUint8(15)
        // Extract payload bytes from the decompressed view
        const payload = new Uint8Array(pView.buffer, pView.byteOffset, pView.byteLength)
        entityCount = EntityDeserializer.applySnapshotDeltaEntities(
          this.#entityRegistry, this.#chunkRegistry, chunkId, payload, payloadOff, ess, flags, messageTick)
      }
    }

    chunk.dirty = true
    return { voxelCount, entityCount }
  }

  /**
   * Apply a tick delta message (delta entities with delta types).
   * @param {DataView} view
   * @param {boolean} compressed
   * @param {ChunkId} chunkId
   * @param {number} messageTick
   * @returns {{voxelCount: number, entityCount: number}}
   */
  #applyTickDelta(view, compressed, chunkId, messageTick) {
    const { chunk, pView, pOff: entityOff, voxelCount } = this.#parseVoxelDeltas(view, compressed, chunkId)

    // Entity section: delta entities with delta types
    let entityCount = 0
    if (entityOff + 4 <= pView.byteLength) {
      const reader = new BufReader(pView, entityOff)
      entityCount = EntityDeserializer.applyDeltaEntities(this.#entityRegistry, this.#chunkRegistry, chunkId, reader, messageTick)
    }

    chunk.dirty = true
    return { voxelCount, entityCount }
  }
}
