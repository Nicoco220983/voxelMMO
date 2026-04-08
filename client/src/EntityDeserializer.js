// @ts-check
import { getEntityDeserializer } from './EntityTypeDeserializers.js'
import { lz4Decompress, BufReader } from './utils.js'

/** @typedef {import('./ChunkRegistry.js').ChunkRegistry} ChunkRegistry */
/** @typedef {import('./types.js').ChunkId} ChunkId */
/** @typedef {import('./EntityRegistry.js').EntityRegistry} EntityRegistry */
/** @typedef {import('./types.js').GlobalEntityId} GlobalEntityId */

/**
 * @class EntityDeserializer
 * @description Client-side entity deserialization utilities.
 * Mirrors the server-side EntitySerializer.cpp for network message parsing.
 *
 * Deserialization formats:
 *   deserializeCreate(): [global_id(4)] [entity_type(1)] [component_mask(1)] [component_data...]
 *   deserializeUpdate(): [global_id(4)] [entity_type(1)] [component_mask(1)] [component_data...]
 *
 * This class is stateless - all methods are pure functions that operate on
 * the provided buffers and delegate entity lifecycle to EntityRegistry.
 */
export class EntityDeserializer {
  /**
   * Parse and apply entity records from a snapshot message.
   * Replaces all entities in the chunk with the new set.
   * @param {EntityRegistry} entityRegistry - The entity registry
   * @param {ChunkRegistry} chunkRegistry - The chunk registry
   * @param {ChunkId} chunkId - The chunk ID
   * @param {DataView} view
   * @param {number} offset Byte offset to start of entity section (entity count int32)
   * @param {number} entitySectionSize Size of entity section in bytes
   * @param {number} messageTick Server tick from message header
   * @returns {number} Number of entities parsed
   */
  static applySnapshotEntities(entityRegistry, chunkRegistry, chunkId, view, offset, entitySectionSize, messageTick) {
    const chunk = chunkRegistry.getOrCreate(chunkId)
    const chunkEntities = chunk.entities

    // Remove existing entities that were in this chunk
    for (const globalId of chunkEntities) {
      const entity = entityRegistry.get(globalId)
      if (entity) entityRegistry.deleteEntity(chunkRegistry, globalId)
    }
    chunkEntities.clear()

    if (entitySectionSize < 4) {
      return 0
    }

    const raw = new Uint8Array(view.buffer, view.byteOffset, view.byteLength)

    // Check if compressed (flags byte at position 15, after 15-byte header)
    const flags = view.getUint8(15)
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
    return this.#applySnapshotEntitiesFromReader(entityRegistry, chunkRegistry, chunkId, reader, messageTick)
  }

  /**
   * Internal method to apply snapshot entities from a BufReader.
   * Used by both snapshot messages and snapshot delta messages.
   * @param {EntityRegistry} entityRegistry - The entity registry
   * @param {ChunkRegistry} chunkRegistry - The chunk registry
   * @param {ChunkId} chunkId - The chunk ID
   * @param {BufReader} reader - Reader positioned at entity count
   * @param {number} messageTick Server tick from message header
   * @returns {number} Number of entities parsed
   * @private
   */
  static #applySnapshotEntitiesFromReader(entityRegistry, chunkRegistry, chunkId, reader, messageTick) {
    const count = reader.readInt32()

    for (let i = 0; i < count; i++) {
      const entityId = reader.readUint32()
      const entityType = reader.readUint8()
      const componentMask = reader.readUint8()

      const deserializer = getEntityDeserializer(entityType)
      if (!deserializer) {
        console.error('[EntityDeserializer] Unknown entity type:', entityType, 'for entity', entityId)
        continue
      }

      // Check if entity exists and message is stale
      const existingEntity = entityRegistry.get(entityId)
      if (existingEntity && messageTick <= existingEntity.lastCreateTick) {
        console.debug('[EntityDeserializer] Discarding stale snapshot entity:', { entityId, messageTick, lastCreateTick: existingEntity.lastCreateTick })
        // Still need to consume the component data in the reader (pass null to read without storing)
        deserializer.deserializeCreate(null, reader, componentMask, messageTick)
        continue
      }

      // Create entity first, then deserialize into it
      const entity = entityRegistry.createEntity(chunkRegistry, entityId, entityType, chunkId)
      if (entity) {
        deserializer.deserializeCreate(entity, reader, componentMask, messageTick)
        entity.markCreated(messageTick)
      } else {
        // Failed to create, consume component data without storing
        deserializer.deserializeCreate(null, reader, componentMask, messageTick)
      }
    }
    return count
  }

  /**
   * Parse and apply entity records from a snapshot delta message.
   * Snapshot deltas now send full entities like regular snapshots.
   * @param {EntityRegistry} entityRegistry - The entity registry
   * @param {ChunkRegistry} chunkRegistry - The chunk registry
   * @param {ChunkId} chunkId - The chunk ID
   * @param {Uint8Array} payload - The decompressed payload bytes
   * @param {number} offset - Byte offset to start of entity data (after entity_section_stored_size)
   * @param {number} entitySectionSize - Size of entity data in bytes (ess field value)
   * @param {number} flags - Flags byte from message header (0x01 = entity section is compressed)
   * @param {number} messageTick Server tick from message header
   * @returns {number} Number of entities parsed
   */
  static applySnapshotDeltaEntities(entityRegistry, chunkRegistry, chunkId, payload, offset, entitySectionSize, flags, messageTick) {
    const chunk = chunkRegistry.getOrCreate(chunkId)
    const chunkEntities = chunk.entities

    // Remove existing entities that were in this chunk
    for (const globalId of chunkEntities) {
      const entity = entityRegistry.get(globalId)
      if (entity) entityRegistry.deleteEntity(chunkRegistry, globalId)
    }
    chunkEntities.clear()

    if (entitySectionSize < 4) {
      return 0
    }

    let entityData

    if (flags & 0x01) {
      // Compressed: int32 uncompressed_size + LZ4 data
      const uncompSize = new DataView(payload.buffer, payload.byteOffset + offset, 4).getInt32(0, true)
      const compressedStart = offset + 4  // skip uncompSize
      entityData = lz4Decompress(payload.subarray(compressedStart, offset + entitySectionSize), uncompSize)
    } else {
      // Uncompressed: entity data directly (starts with entity_count int32)
      entityData = payload.subarray(offset, offset + entitySectionSize)
    }

    const entView = new DataView(entityData.buffer, entityData.byteOffset, entityData.byteLength)
    const reader = new BufReader(entView)
    return this.#applySnapshotEntitiesFromReader(entityRegistry, chunkRegistry, chunkId, reader, messageTick)
  }

  /**
   * Parse and apply entity deltas from a delta message.
   * Handles CREATE_ENTITY (new), UPDATE_ENTITY (update), DELETE_ENTITY (remove),
   * and CHUNK_CHANGE_ENTITY (entity moved to different chunk).
   * @param {EntityRegistry} entityRegistry - The entity registry
   * @param {ChunkRegistry} chunkRegistry - The chunk registry
   * @param {ChunkId} chunkId - The source chunk ID
   * @param {BufReader} reader Positioned at entity count
   * @param {number} messageTick Server tick from message header
   * @returns {number} Number of entity deltas parsed
   */
  static applyDeltaEntities(entityRegistry, chunkRegistry, chunkId, reader, messageTick) {
    const DeltaType = {
      CREATE_ENTITY: 0,
      UPDATE_ENTITY: 1,
      DELETE_ENTITY: 2,
      CHUNK_CHANGE_ENTITY: 3
    }

    const count = reader.readInt32()

    for (let i = 0; i < count; i++) {
      const deltaType = reader.readUint8()
      const entityId = reader.readUint32()

      if (deltaType === DeltaType.DELETE_ENTITY) {
        const entity = entityRegistry.get(entityId)
        // Discard stale delete
        if (entity && messageTick <= entity.lastCreateTick) {
          console.debug('[EntityDeserializer] Discarding stale DELETE_ENTITY:', { entityId, messageTick, lastCreateTick: entity.lastCreateTick })
          continue
        }
        entityRegistry.deleteEntity(chunkRegistry, entityId)
        continue
      }

      if (deltaType === DeltaType.CHUNK_CHANGE_ENTITY) {
        const newChunkIdPacked = reader.readInt64()
        const entity = entityRegistry.get(entityId)
        // Discard stale chunk change
        if (entity && messageTick <= entity.lastCreateTick) {
          console.debug('[EntityDeserializer] Discarding stale CHUNK_CHANGE_ENTITY:', { entityId, messageTick, lastCreateTick: entity.lastCreateTick })
          continue
        }
        // Only remove if the target chunk is not registered
        if (!chunkRegistry.has(newChunkIdPacked)) {
          console.debug('[EntityDeserializer] Entity moved to unregistered chunk, removing:', { entityId, newChunkId: newChunkIdPacked.toString() })
          entityRegistry.deleteEntity(chunkRegistry, entityId)
        }
        // CHUNK_CHANGE has no additional data (entityType/component data)
        continue
      }

      if (deltaType === DeltaType.CREATE_ENTITY || deltaType === DeltaType.UPDATE_ENTITY) {
        // CREATE_ENTITY or UPDATE_ENTITY - both have entityType + component data
        const entityType = reader.readUint8()
        const componentMask = reader.readUint8()

        let entity = entityRegistry.get(entityId)

        // Protocol error: UPDATE_ENTITY for unknown entity
        if (!entity && deltaType === DeltaType.UPDATE_ENTITY) {
          console.error('[EntityDeserializer] UPDATE_ENTITY for unknown entity:', entityId)
          continue
        }

        const deserializer = getEntityDeserializer(entityType)
        if (!deserializer) {
          console.error('[EntityDeserializer] Unknown entity type:', entityType, 'for entity', entityId)
          continue
        }

        // Check if message is stale (tick <= lastCreateTick)
        if (entity && messageTick <= entity.lastCreateTick) {
          console.debug('[EntityDeserializer] Discarding stale delta entity:', { entityId, deltaType, messageTick, lastCreateTick: entity.lastCreateTick })
          // Consume component data without storing (pass null to entity parameter)
          if (deltaType === DeltaType.CREATE_ENTITY) {
            deserializer.deserializeCreate(null, reader, componentMask, messageTick)
          } else {
            deserializer.deserializeUpdate(null, reader, componentMask, messageTick)
          }
          continue
        }

        if (!entity) {
          // CREATE_ENTITY: create new entity, then deserialize into it
          entity = entityRegistry.createEntity(chunkRegistry, entityId, entityType, chunkId)
          if (!entity) {
            // Failed to create, consume component data without storing
            deserializer.deserializeCreate(null, reader, componentMask, messageTick)
            continue
          }
          deserializer.deserializeCreate(entity, reader, componentMask, messageTick)
          entity.markCreated(messageTick)
        } else {
          // UPDATE_ENTITY: update existing entity
          console.debug('[EntityDeserializer] Updating entity:', { entityId, entityType })
          deserializer.deserializeUpdate(entity, reader, componentMask, messageTick)
        }

        // Note: Chunk boundary detection is now handled by ChunkMembershipSystem
        // which runs every tick in GameClient.updateEntities()
      }
    }
    return count
  }
}
