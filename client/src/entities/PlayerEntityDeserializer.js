// @ts-check
/**
 * @file PlayerEntityDeserializer.js
 * @description Client-side deserializer for PLAYER entity type.
 * Mirrors server/game/entities/PlayerEntity.cpp serialization.
 *
 * Deserialization formats:
 *   deserializeCreate(): [component_mask(1)] [position_data...]
 *   deserializeUpdate(): [component_mask(1)] [position_data... if POSITION_BIT]
 */

import { PlayerEntity } from './PlayerEntity.js'
import { EntityType, POSITION_BIT } from '../types.js'

/** @typedef {import('../utils.js').BufReader} BufReader */
/** @typedef {import('../types.js').GlobalEntityId} GlobalEntityId */
/** @typedef {import('../types.js').ChunkId} ChunkId */
/** @typedef {import('../ChunkRegistry.js').ChunkRegistry} ChunkRegistry */
/** @typedef {import('../EntityRegistry.js').EntityRegistry} EntityRegistry */

/**
 * Deserialize player entity creation.
 * @param {EntityRegistry} entityRegistry
 * @param {ChunkRegistry} chunkRegistry
 * @param {GlobalEntityId} entityId
 * @param {ChunkId} chunkId
 * @param {BufReader} reader
 * @param {number} componentMask
 * @param {number} messageTick
 * @returns {PlayerEntity|null}
 */
export function deserializeCreate(entityRegistry, chunkRegistry, entityId, chunkId, reader, componentMask, messageTick) {
  const entity = /** @type {PlayerEntity} */ (entityRegistry.createEntity(chunkRegistry, entityId, EntityType.PLAYER, chunkId))
  if (!entity) return null

  if (componentMask & POSITION_BIT) {
    entity.motion.deserialize(reader, messageTick)
  }

  return entity
}

/**
 * Deserialize player entity update.
 * @param {PlayerEntity} entity
 * @param {BufReader} reader
 * @param {number} componentMask
 * @param {number} messageTick
 */
export function deserializeUpdate(entity, reader, componentMask, messageTick) {
  if (componentMask & POSITION_BIT) {
    entity.motion.deserialize(reader, messageTick)
  }
}
