// @ts-check
/**
 * @file EntityCatalog.js
 * @description Entity type definitions and class mappings.
 * Central registry for entity types and their corresponding classes.
 *
 * Mirrors server EntityType.hpp and provides client-side class lookup.
 */

import { PlayerEntity } from './entities/PlayerEntity.js'
import { SheepEntity } from './entities/SheepEntity.js'
import { GoblinEntity } from './entities/GoblinEntity.js'

/**
 * Known entity types (must stay in sync with server EntityType.hpp).
 * @readonly
 * @enum {number}
 */
export const EntityType = Object.freeze({
  PLAYER:       0,  // Full-physics player (gravity + collision)
  GHOST_PLAYER: 1,  // Ghost player (noclip, no gravity)
  SHEEP:        2,  // Passive mob: wanders randomly, blocked by voxels
  GOBLIN:       3,  // Hostile mob: wanders, chases and attacks players
})

/** @type {Record<number, string>} Maps EntityType value to name. */
const ENTITY_TYPE_NAMES = Object.freeze({
  [EntityType.PLAYER]:       'PLAYER',
  [EntityType.GHOST_PLAYER]: 'GHOST_PLAYER',
  [EntityType.SHEEP]:        'SHEEP',
  [EntityType.GOBLIN]:       'GOBLIN',
})

/** @type {Record<string, number>} Maps lowercase name to EntityType value. */
const ENTITY_TYPE_BY_NAME = Object.freeze({
  'player':       EntityType.PLAYER,
  'ghost_player': EntityType.GHOST_PLAYER,
  'sheep':        EntityType.SHEEP,
  'goblin':       EntityType.GOBLIN,
})

/**
 * Convert EntityType enum value to human-readable string name.
 * @param {number} type - The entity type value.
 * @returns {string} The type name (e.g., "PLAYER", "SHEEP") or "UNKNOWN".
 */
export function entityTypeToString(type) {
  return ENTITY_TYPE_NAMES[type] ?? 'UNKNOWN'
}

/**
 * Parse entity type from string name (case-insensitive).
 * @param {string} str - The string to parse (e.g., "sheep", "PLAYER").
 * @returns {number|null} The EntityType value, or null if unrecognized.
 */
export function stringToEntityType(str) {
  return ENTITY_TYPE_BY_NAME[str.toLowerCase()] ?? null
}

/**
 * @typedef {Object} EntityClassEntry
 * @property {Function} entityClass - The entity class constructor
 * @property {Function} deserializeCreate - Static method to deserialize entity creation
 * @property {Function} deserializeUpdate - Static method to deserialize entity update
 */

/**
 * Entity class table indexed by EntityType.
 * Each entry provides the entity class and its static deserialization methods.
 *
 * @type {EntityClassEntry[]}
 */
export const ENTITY_CLASS_TABLE = [
  // EntityType::PLAYER = 0
  {
    entityClass: PlayerEntity,
    deserializeCreate: PlayerEntity.deserializeCreate.bind(PlayerEntity),
    deserializeUpdate: PlayerEntity.deserializeUpdate.bind(PlayerEntity)
  },
  // EntityType::GHOST_PLAYER = 1
  {
    entityClass: PlayerEntity,
    deserializeCreate: PlayerEntity.deserializeCreateGhost.bind(PlayerEntity),
    deserializeUpdate: PlayerEntity.deserializeUpdate.bind(PlayerEntity)
  },
  // EntityType::SHEEP = 2
  {
    entityClass: SheepEntity,
    deserializeCreate: SheepEntity.deserializeCreate.bind(SheepEntity),
    deserializeUpdate: SheepEntity.deserializeUpdate.bind(SheepEntity)
  },
  // EntityType::GOBLIN = 3
  {
    entityClass: GoblinEntity,
    deserializeCreate: GoblinEntity.deserializeCreate.bind(GoblinEntity),
    deserializeUpdate: GoblinEntity.deserializeUpdate.bind(GoblinEntity)
  }
]

/**
 * Helper to look up entity class entry for an entity type.
 * @param {number} type - EntityType value
 * @returns {EntityClassEntry|undefined}
 */
export function getEntityClass(type) {
  return ENTITY_CLASS_TABLE[type]
}
