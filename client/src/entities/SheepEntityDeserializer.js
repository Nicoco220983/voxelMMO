// @ts-check
/**
 * @file SheepEntityDeserializer.js
 * @description Client-side deserializer for SHEEP entity type.
 * Mirrors server/game/entities/SheepEntity.cpp serialization.
 *
 * Deserialization formats:
 *   deserializeCreate(): [component_mask(1)] [position_data...] [behavior_data...]
 *   deserializeUpdate(): [component_mask(1)] [position_data... if POSITION_BIT] [behavior_data... if SHEEP_BEHAVIOR_BIT]
 */

import { SheepEntity } from './SheepEntity.js'
import { EntityType, POSITION_BIT, SHEEP_BEHAVIOR_BIT } from '../types.js'

/** @typedef {import('../utils.js').BufReader} BufReader */
/** @typedef {import('../types.js').GlobalEntityId} GlobalEntityId */
/** @typedef {import('../types.js').ChunkId} ChunkId */
/** @typedef {import('../ChunkRegistry.js').ChunkRegistry} ChunkRegistry */
/** @typedef {import('../EntityRegistry.js').EntityRegistry} EntityRegistry */

/**
 * Deserialize sheep entity creation.
 * @param {EntityRegistry} entityRegistry
 * @param {ChunkRegistry} chunkRegistry
 * @param {GlobalEntityId} entityId
 * @param {ChunkId} chunkId
 * @param {BufReader} reader
 * @param {number} componentMask
 * @param {number} messageTick
 * @returns {SheepEntity|null}
 */
export function deserializeCreate(entityRegistry, chunkRegistry, entityId, chunkId, reader, componentMask, messageTick) {
  const entity = /** @type {SheepEntity} */ (entityRegistry.createEntity(chunkRegistry, entityId, EntityType.SHEEP, chunkId))
  if (!entity) return null

  if (componentMask & POSITION_BIT) {
    entity.motion.deserialize(reader, messageTick)
  }

  if (componentMask & SHEEP_BEHAVIOR_BIT) {
    entity.behavior.deserialize(reader)
  }

  return entity
}

/**
 * Deserialize sheep entity update.
 * @param {SheepEntity} entity
 * @param {BufReader} reader
 * @param {number} componentMask
 * @param {number} messageTick
 */
export function deserializeUpdate(entity, reader, componentMask, messageTick) {
  if (componentMask & POSITION_BIT) {
    entity.motion.deserialize(reader, messageTick)
  }

  if (componentMask & SHEEP_BEHAVIOR_BIT) {
    entity.behavior.deserialize(reader)
  }
}
