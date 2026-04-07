// @ts-check
/**
 * @file EntityTypeDeserializers.js
 * @description Client-side entity type deserializer table.
 * Imports deserializers from individual entity type files and exports the lookup table.
 *
 * Mirrors server/game/EntityTypeSerializers.cpp
 *
 * Usage:
 *   import { getEntityDeserializer, ENTITY_DESERIALIZER_TABLE } from './EntityTypeDeserializers.js'
 *   const table = getEntityDeserializer(entityType)
 *   const entity = table.deserializeCreate(registry, chunkRegistry, id, chunkId, reader, mask, tick)
 *   table.deserializeUpdate(entity, reader, mask, tick)
 */

import * as PlayerEntityDeserializer from './entities/PlayerEntityDeserializer.js'
import * as GhostPlayerEntityDeserializer from './entities/GhostPlayerEntityDeserializer.js'
import * as SheepEntityDeserializer from './entities/SheepEntityDeserializer.js'

/**
 * @typedef {Object} EntityDeserializerTable
 * @property {Function} deserializeCreate - Function to deserialize entity creation: (registry, chunkRegistry, entityId, chunkId, reader, componentMask, messageTick) => entity
 * @property {Function} deserializeUpdate - Function to deserialize entity update: (entity, reader, componentMask, messageTick) => void
 */

/**
 * Deserializer function table indexed by EntityType.
 * Mirrors server/game/EntityTypeSerializers.cpp ENTITY_SERIALIZER_TABLE.
 *
 * @type {EntityDeserializerTable[]}
 */
export const ENTITY_DESERIALIZER_TABLE = [
  // EntityType::PLAYER = 0
  PlayerEntityDeserializer,
  // EntityType::GHOST_PLAYER = 1
  GhostPlayerEntityDeserializer,
  // EntityType::SHEEP = 2
  SheepEntityDeserializer
]

/**
 * Helper to look up deserializer table entry for an entity type.
 * @param {number} type - EntityType value
 * @returns {EntityDeserializerTable|undefined}
 */
export function getEntityDeserializer(type) {
  return ENTITY_DESERIALIZER_TABLE[type]
}
