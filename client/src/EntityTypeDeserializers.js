// @ts-check
/**
 * @file EntityTypeDeserializers.js
 * @description Client-side entity type deserializer table.
 * Exports deserializer lookup table using static methods from entity classes.
 *
 * Mirrors server/game/EntityTypeSerializers.cpp
 *
 * Usage:
 *   import { getEntityDeserializer, ENTITY_DESERIALIZER_TABLE } from './EntityTypeDeserializers.js'
 *   const table = getEntityDeserializer(entityType)
 *   const entity = table.deserializeCreate(registry, chunkRegistry, id, chunkId, reader, mask, tick)
 *   table.deserializeUpdate(entity, reader, mask, tick)
 */

import { PlayerEntity } from './entities/PlayerEntity.js'
import { SheepEntity } from './entities/SheepEntity.js'

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
  {
    deserializeCreate: PlayerEntity.deserializeCreate.bind(PlayerEntity),
    deserializeUpdate: PlayerEntity.deserializeUpdate.bind(PlayerEntity)
  },
  // EntityType::GHOST_PLAYER = 1
  {
    deserializeCreate: PlayerEntity.deserializeCreateGhost.bind(PlayerEntity),
    deserializeUpdate: PlayerEntity.deserializeUpdate.bind(PlayerEntity)
  },
  // EntityType::SHEEP = 2
  {
    deserializeCreate: SheepEntity.deserializeCreate.bind(SheepEntity),
    deserializeUpdate: SheepEntity.deserializeUpdate.bind(SheepEntity)
  }
]

/**
 * Helper to look up deserializer table entry for an entity type.
 * @param {number} type - EntityType value
 * @returns {EntityDeserializerTable|undefined}
 */
export function getEntityDeserializer(type) {
  return ENTITY_DESERIALIZER_TABLE[type]
}
