// @ts-check
import { BaseEntity } from './entities/BaseEntity.js'
import { SheepEntity } from './entities/SheepEntity.js'
import { PlayerEntity } from './entities/PlayerEntity.js'
import { EntityType, CREATED_BIT, POSITION_BIT } from './types.js'
import { DeltaType } from './NetworkProtocol.js'
import { lz4Decompress, BufReader } from './utils.js'
import { ChunkId } from './Chunk.js'

/** @typedef {import('./ChunkRegistry.js').ChunkRegistry} ChunkRegistry */

/**
 * @class EntityRegistry
 * @description Global entity registry. Entities have stable global IDs and can move
 * between chunks. This class manages only the global entity map; chunk membership
 * is tracked by each Chunk's `entities` Set (managed by ChunkRegistry).
 *
 * Design principles:
 * - Entities are global objects keyed by GlobalEntityId (uint32)
 * - Each entity tracks its current chunkId (stored in entity.chunkId)
 * - Chunk membership is tracked per-chunk via Chunk.entities Set
 * - When an entity moves chunks, it updates its chunkId; no new entity is created
 */
export class EntityRegistry {
  /** @type {Map<number, BaseEntity>} GlobalEntityId → entity */
  #entities = new Map()

  /** @type {THREE.Scene|null} Scene reference for creating entity meshes */
  scene = null

  /**
   * Set the scene for entity mesh creation.
   * @param {THREE.Scene} scene
   */
  setScene(scene) {
    this.scene = scene
  }

  /**
   * Create an entity and register it in both global registry and chunk membership.
   * @param {ChunkRegistry} chunkRegistry
   * @param {number} entityId
   * @param {number} entityType
   * @param {ChunkIdPacked} chunkId
   * @returns {BaseEntity} The created entity
   * @private
   */
  #createEntity(chunkRegistry, entityId, entityType, chunkId) {
    let entity = null
    if (entityType === EntityType.SHEEP) {
      entity = new SheepEntity(entityId, this.scene)
    } else if (entityType === EntityType.PLAYER || entityType === EntityType.GHOST_PLAYER) {
      entity = new PlayerEntity(entityId, entityType, this.scene)
    } else {
      console.error('[EntityRegistry] Unknown entity type:', entityType, 'for entity', entityId)
      return null
    }
    if(!entity) return null
    this.#entities.set(entityId, entity)
    entity.chunkId = chunkId
    const chunk = chunkRegistry.getOrCreate(chunkId)
    chunk.entities.add(entityId)
    return entity
  }

  /**
   * Get an entity by its global ID.
   * @param {number} globalId
   * @returns {BaseEntity|undefined}
   */
  get(globalId) {
    return this.#entities.get(globalId)
  }

  /**
   * Check if an entity exists.
   * @param {number} globalId
   * @returns {boolean}
   */
  has(globalId) {
    return this.#entities.has(globalId)
  }

  /**
   * Get all entities.
   * @returns {IterableIterator<BaseEntity>}
   */
  *all() {
    yield* this.#entities.values()
  }

  /**
   * Clear all entities.
   */
  clear() {
    this.#entities.clear()
  }

  /**
   * Delete an entity and remove it from chunk membership.
   * Gets the chunk ID from entity.chunkId.
   * @param {ChunkRegistry} chunkRegistry
   * @param {number} entityId
   */
  deleteEntity(chunkRegistry, entityId) {
    const entity = this.#entities.get(entityId)
    if (entity) {
      console.debug('[EntityRegistry] Deleting entity:', { entityId })
      if (entity.destroy && this.scene) {
        entity.destroy(this.scene)
      }
      this.#entities.delete(entityId)
      // Remove from chunk's entities using entity's stored chunkId
      if (entity.chunkId !== undefined) {
        const chunk = chunkRegistry.get(entity.chunkId)
        if (chunk) chunk.entities.delete(entityId)
      }
    }
  }

  /**
   * Parse and apply entity records from a snapshot message.
   * Replaces all entities in the chunk with the new set.
   * @param {ChunkRegistry} chunkRegistry - The chunk registry
   * @param {ChunkIdPacked} chunkId - The chunk ID
   * @param {DataView} view
   * @param {number} offset Byte offset to start of entity section (entity count int32)
   * @param {number} entitySectionSize Size of entity section in bytes
   * @param {number} messageTick Server tick from message header
   * @returns {number} Number of entities parsed
   */
  applySnapshotEntities(chunkRegistry, chunkId, view, offset, entitySectionSize, messageTick) {
    const chunk = chunkRegistry.getOrCreate(chunkId)
    const chunkEntities = chunk.entities
    
    // Remove existing entities that were in this chunk
    for (const globalId of chunkEntities) {
      const entity = this.#entities.get(globalId)
      if (entity) this.#entities.delete(globalId)
    }
    chunkEntities.clear()

    if (entitySectionSize < 4) {
      return 0
    }

    const raw = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)

    // Check if compressed (flags byte at position 13)
    const flags = view.getUint8(13)
    let entityData

    if (flags & 0x01) {
      // Compressed: int32 uncompressed_size + LZ4 data
      const uncompSize = new DataView(raw.buffer, raw.byteOffset + offset, 4).getInt32(0, true)
      entityData = lz4Decompress(raw.subarray(offset + 4, offset + entitySectionSize), uncompSize)
    } else {
      entityData = raw.subarray(offset, offset + entitySectionSize)
    }

    const entView = new DataView(entityData.buffer, entityData.byteOffset, entityData.byteLength)
    const reader = new BufReader(entView)
    const count = reader.readInt32()

    for (let i = 0; i < count; i++) {
      const id = reader.readUint32()
      const type = reader.readUint8()
      const componentFlags = reader.readUint8()

      const entity = this.#createEntity(chunkRegistry, id, type, chunkId)
      entity.applyComponents(reader, componentFlags, messageTick)
    }
    return count
  }

  /**
   * Parse and apply entity deltas from a delta message.
   * Handles CREATE_ENTITY (new), UPDATE_ENTITY (update), DELETE_ENTITY (remove),
   * and CHUNK_CHANGE_ENTITY (entity moved to different chunk).
   * @param {ChunkRegistry} chunkRegistry - The chunk registry
   * @param {ChunkIdPacked} chunkId - The source chunk ID
   * @param {BufReader} reader Positioned at entity count
   * @param {number} messageTick Server tick from message header
   * @returns {number} Number of entity deltas parsed
   */
  applyDeltaEntities(chunkRegistry, chunkId, reader, messageTick) {
    const count = reader.readInt32()

    for (let i = 0; i < count; i++) {
      const deltaType = reader.readUint8()
      const entityId = reader.readUint32()

      if (deltaType === DeltaType.DELETE_ENTITY) {
        this.deleteEntity(chunkRegistry, entityId)
      } else if (deltaType === DeltaType.CHUNK_CHANGE_ENTITY) {
        // Only remove if the target chunk is not registered
        // The actual entity.chunkId update is handled automatically by ChunkMembershipSystem
        const newChunkIdPacked = reader.readInt64()
        if (!chunkRegistry.has(newChunkIdPacked)) {
          console.debug('[EntityRegistry] Entity moved to unregistered chunk, removing:', { entityId, newChunkId: newChunkId.toString() })
          this.deleteEntity(chunkRegistry, entityId)
        }
      } else if (deltaType === DeltaType.CREATE_ENTITY || deltaType === DeltaType.UPDATE_ENTITY) {
        // CREATE_ENTITY or UPDATE_ENTITY - both have entityType + component data
        const entityType = reader.readUint8()
        const componentMask = reader.readUint8()
        
        let entity = this.#entities.get(entityId)
        
        // Protocol error: UPDATE_ENTITY for unknown entity without CREATED flag
        if (deltaType === DeltaType.UPDATE_ENTITY && !entity && !(componentMask & CREATED_BIT)) {
          console.error('[EntityRegistry] UPDATE_ENTITY for unknown entity without CREATED flag:', entityId)
          continue
        }
        
        const wasCreate = !entity
        if (wasCreate) {
          console.debug('[EntityRegistry] Creating entity:', { entityId, entityType, isCreate: deltaType === DeltaType.CREATE_ENTITY })
          entity = this.#createEntity(chunkRegistry, entityId, entityType, chunkId)
        } else {
          console.debug('[EntityRegistry] Updating entity:', { entityId, entityType })
        }
        entity.applyComponents(reader, componentMask, messageTick)
        
        // Note: Chunk boundary detection is now handled by ChunkMembershipSystem
        // which runs every tick in GameClient.updateEntities()
      } else {
        console.error('[EntityRegistry] Unknown delta type:', deltaType, 'for entity', entityId)
      }
    }
    return count
  }
}
