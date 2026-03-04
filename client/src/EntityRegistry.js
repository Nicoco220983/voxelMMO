// @ts-check
import { BaseEntity } from './entities/BaseEntity.js'
import { DeltaType } from './types.js'
import { lz4Decompress, BufReader } from './utils.js'

/** @typedef {import('./types.js').ChunkIdPacked} ChunkIdPacked */

/**
 * @class EntityRegistry
 * @description Global entity registry. Entities have stable global IDs and can move
 * between chunks. This class manages the global entity map and per-chunk membership.
 *
 * Design principles:
 * - Entities are global objects keyed by GlobalEntityId (uint32)
 * - Each entity tracks its current chunkId (stored in entity.chunkId)
 * - Chunks track which entity IDs they currently contain (for cleanup)
 * - When an entity moves chunks, it updates its chunkId; no new entity is created
 */
export class EntityRegistry {
  /** @type {Map<number, BaseEntity>} GlobalEntityId → entity */
  #entities = new Map()

  /** @type {Map<ChunkIdPacked, Set<number>>} ChunkId → Set of GlobalEntityIds */
  #chunkMembers = new Map()

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
   * Get all entities currently in a specific chunk.
   * @param {ChunkIdPacked} chunkId
   * @returns {IterableIterator<BaseEntity>}
   */
  *inChunk(chunkId) {
    const members = this.#chunkMembers.get(chunkId)
    if (!members) return
    for (const globalId of members) {
      const entity = this.#entities.get(globalId)
      if (entity) yield entity
    }
  }

  /**
   * Clear all entities and chunk memberships.
   */
  clear() {
    this.#entities.clear()
    this.#chunkMembers.clear()
  }

  /**
   * Remove all entities belonging to a specific chunk.
   * Called when a chunk is unloaded.
   * @param {ChunkIdPacked} chunkId
   */
  removeChunk(chunkId) {
    const members = this.#chunkMembers.get(chunkId)
    if (!members) return
    for (const globalId of members) {
      const entity = this.#entities.get(globalId)
      // Only delete if entity is still in this chunk (might have moved)
      if (entity && entity.chunkId === chunkId) {
        this.#entities.delete(globalId)
      }
    }
    this.#chunkMembers.delete(chunkId)
  }

  /**
   * Parse and apply entity records from a snapshot message.
   * Replaces all entities in the chunk with the new set.
   * @param {ChunkIdPacked} chunkId
   * @param {DataView} view
   * @param {number} offset Byte offset to start of entity section (entity count int32)
   * @param {number} entitySectionSize Size of entity section in bytes
   * @param {number} messageTick Server tick from message header
   */
  applySnapshotEntities(chunkId, view, offset, entitySectionSize, messageTick) {
    // Remove existing members of this chunk
    const oldMembers = this.#chunkMembers.get(chunkId)
    if (oldMembers) {
      for (const globalId of oldMembers) {
        const entity = this.#entities.get(globalId)
        if (entity && entity.chunkId === chunkId) {
          this.#entities.delete(globalId)
        }
      }
      oldMembers.clear()
    }

    if (entitySectionSize < 4) {
      return
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

    const members = this.#getOrCreateMembers(chunkId)

    for (let i = 0; i < count; i++) {
      const id = reader.readUint32()
      const type = reader.readUint8()
      const componentFlags = reader.readUint8()

      const entity = new BaseEntity(id, type)
      entity.chunkId = chunkId
      entity.applyComponents(reader, componentFlags, messageTick)

      this.#entities.set(id, entity)
      members.add(id)
    }
  }

  /**
   * Parse and apply entity deltas from a delta message.
   * Handles DELETE (remove), NEW_ENTITY/UPDATE_ENTITY (upsert + update).
   * @param {ChunkIdPacked} chunkId
   * @param {BufReader} reader Positioned at entity count
   * @param {number} messageTick Server tick from message header
   */
  applyDeltaEntities(chunkId, reader, messageTick) {
    const count = reader.readInt32()
    const members = this.#getOrCreateMembers(chunkId)

    for (let i = 0; i < count; i++) {
      const deltaType = reader.readUint8()
      const entityId = reader.readUint32()
      const entityType = reader.readUint8()

      if (deltaType === DeltaType.DELETE) {
        const entity = this.#entities.get(entityId)
        if (entity && entity.chunkId === chunkId) {
          this.#entities.delete(entityId)
        }
        members.delete(entityId)
      } else {
        // NEW_ENTITY or UPDATE_ENTITY - upsert
        let entity = this.#entities.get(entityId)
        if (!entity) {
          entity = new BaseEntity(entityId, entityType)
          this.#entities.set(entityId, entity)
        }
        entity.chunkId = chunkId
        members.add(entityId)
        entity.applyDelta(reader, messageTick)
      }
    }
  }

  /**
   * @param {ChunkIdPacked} chunkId
   * @returns {Set<number>}
   */
  #getOrCreateMembers(chunkId) {
    let members = this.#chunkMembers.get(chunkId)
    if (!members) {
      members = new Set()
      this.#chunkMembers.set(chunkId, members)
    }
    return members
  }
}
