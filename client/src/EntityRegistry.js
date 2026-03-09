// @ts-check
import { BaseEntity } from './entities/BaseEntity.js'
import { SheepEntity } from './entities/SheepEntity.js'
import { PlayerEntity } from './entities/PlayerEntity.js'
import { EntityType, CREATED_BIT } from './types.js'
import { DeltaType } from './NetworkProtocol.js'
import { lz4Decompress, BufReader } from './utils.js'

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
   * Factory method to create appropriate entity type.
   * @param {number} globalId
   * @param {number} entityType
   * @returns {BaseEntity}
   */
  #createEntity(globalId, entityType) {
    if (!this.scene) {
      return new BaseEntity(globalId, entityType)
    }
    if (entityType === EntityType.SHEEP) {
      return new SheepEntity(globalId, this.scene)
    }
    if (entityType === EntityType.PLAYER || entityType === EntityType.GHOST_PLAYER) {
      return new PlayerEntity(globalId, entityType, this.scene)
    }
    if (!Object.values(EntityType).includes(entityType)) {
      console.error('[EntityRegistry] Unknown entity type:', entityType, 'for entity', globalId)
    }
    return new BaseEntity(globalId, entityType)
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
   * Remove a specific entity by its global ID.
   * Calls destroy() on the entity if it has a mesh.
   * @param {number} globalId
   * @returns {boolean} True if entity was removed
   */
  remove(globalId) {
    const entity = this.#entities.get(globalId)
    if (!entity) return false
    if (entity.destroy && this.scene) {
      entity.destroy(this.scene)
    }
    this.#entities.delete(globalId)
    return true
  }

  /**
   * Parse and apply entity records from a snapshot message.
   * Replaces all entities in the chunk with the new set.
   * The caller is responsible for managing chunk.entities Set.
   * @param {Set<number>} chunkEntities - The chunk's entities Set to populate
   * @param {DataView} view
   * @param {number} offset Byte offset to start of entity section (entity count int32)
   * @param {number} entitySectionSize Size of entity section in bytes
   * @param {number} messageTick Server tick from message header
   * @param {ChunkIdPacked} chunkId - The chunk ID for setting entity.chunkId
   * @returns {number} Number of entities parsed
   */
  applySnapshotEntities(chunkEntities, view, offset, entitySectionSize, messageTick, chunkId) {
    // Remove existing entities that were in this chunk
    for (const globalId of chunkEntities) {
      const entity = this.#entities.get(globalId)
      // Only delete if entity is still in this chunk (might have moved)
      if (entity && entity.chunkId === chunkId) {
        this.#entities.delete(globalId)
      }
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

      const entity = this.#createEntity(id, type)
      entity.chunkId = chunkId
      entity.applyComponents(reader, componentFlags, messageTick)

      this.#entities.set(id, entity)
      chunkEntities.add(id)
    }
    return count
  }

  /**
   * Parse and apply entity deltas from a delta message.
   * Handles CREATE_ENTITY (new), UPDATE_ENTITY (update), DELETE_ENTITY (remove),
   * and CHUNK_CHANGE_ENTITY (entity moved to different chunk).
   * The caller is responsible for managing chunk.entities Set and target chunk's entities Set.
   * @param {Set<number>} chunkEntities - The source chunk's entities Set
   * @param {BufReader} reader Positioned at entity count
   * @param {number} messageTick Server tick from message header
   * @param {ChunkIdPacked} chunkId - The source chunk ID
   * @param {(chunkId: ChunkIdPacked) => Set<number>|undefined} getChunkEntities - Function to get target chunk's entities Set
   * @returns {number} Number of entity deltas parsed
   */
  applyDeltaEntities(chunkEntities, reader, messageTick, chunkId, getChunkEntities) {
    const count = reader.readInt32()

    for (let i = 0; i < count; i++) {
      const deltaType = reader.readUint8()
      const entityId = reader.readUint32()

      if (deltaType === DeltaType.DELETE_ENTITY) {
        // Entity removed from this chunk (despawned or moved elsewhere)
        const entity = this.#entities.get(entityId)
        if (entity) {
          console.debug('[EntityRegistry] Deleting entity:', { entityId })
          // Call destroy if entity has a mesh
          if (entity.destroy && this.scene) {
            entity.destroy(this.scene)
          }
          this.#entities.delete(entityId)
        }
        chunkEntities.delete(entityId)
      } else if (deltaType === DeltaType.CHUNK_CHANGE_ENTITY) {
        // Entity moved to different chunk - read new chunk ID
        const newChunkIdPacked = reader.readInt64()
        const entity = this.#entities.get(entityId)
        if (entity && entity.chunkId === chunkId) {
          console.debug('[EntityRegistry] Changing chunk entity:', { entityId })
          // Update entity's chunk reference
          entity.chunkId = newChunkIdPacked
          // Remove from old chunk's members
          chunkEntities.delete(entityId)
          // Add to new chunk's members (via callback)
          const newChunkEntities = getChunkEntities(newChunkIdPacked)
          if (newChunkEntities) {
            newChunkEntities.add(entityId)
          }
        }
      } else if (deltaType === DeltaType.CREATE_ENTITY || deltaType === DeltaType.UPDATE_ENTITY) {
        // CREATE_ENTITY or UPDATE_ENTITY - both have entityType + component data
        const entityType = reader.readUint8()
        const componentMask = reader.readUint8()
        
        let entity = this.#entities.get(entityId)
        
        // Protocol error: UPDATE_ENTITY for unknown entity without CREATED flag
        if (deltaType === DeltaType.UPDATE_ENTITY && !entity && !(componentMask & CREATED_BIT)) {
          console.error('[EntityRegistry] UPDATE_ENTITY for unknown entity without CREATED flag:', entityId)
        }
        
        if (!entity) {
          console.debug('[EntityRegistry] Creating entity:', { entityId, entityType, isCreate: deltaType === DeltaType.CREATE_ENTITY })
          entity = this.#createEntity(entityId, entityType)
          this.#entities.set(entityId, entity)
        } else {
          console.debug('[EntityRegistry] Updating entity:', { entityId, entityType })
        }
        entity.chunkId = chunkId
        chunkEntities.add(entityId)
        entity.applyComponents(reader, componentMask, messageTick)
      } else {
        console.error('[EntityRegistry] Unknown delta type:', deltaType, 'for entity', entityId)
      }
    }
    return count
  }
}
