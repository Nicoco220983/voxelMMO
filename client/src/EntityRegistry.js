// @ts-check
import { BaseEntity } from './entities/BaseEntity.js'
import { SheepEntity } from './entities/SheepEntity.js'
import { GoblinEntity } from './entities/GoblinEntity.js'
import { PlayerEntity } from './entities/PlayerEntity.js'
import { EntityType } from './types.js'

/** @typedef {import('./ChunkRegistry.js').ChunkRegistry} ChunkRegistry */
/** @typedef {import('./types.js').ChunkId} ChunkId */

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
 *
 * Note: Entity deserialization is handled by EntityDeserializer.js (mirrors
 * server-side EntitySerializer.cpp).
 */
export class EntityRegistry {
  /** @type {Map<GlobalEntityId, BaseEntity>} GlobalEntityId → entity */
  #entities = new Map()

  /** @type {THREE.Scene|null} Scene reference for creating entity meshes */
  scene = null

  /** @type {GlobalEntityId|null} GlobalEntityId of the local player */
  selfEntityId = null

  /**
   * Set the scene for entity mesh creation.
   * @param {THREE.Scene} scene
   */
  setScene(scene) {
    this.scene = scene
  }

  /**
   * Set the self entity ID to hide the local player's mesh.
   * If the entity already exists, mark it as self to hide its mesh.
   * @param {GlobalEntityId|null} entityId
   */
  setSelfEntityId(entityId) {
    this.selfEntityId = entityId

    // If entity already exists, mark it as self to hide its mesh
    if (entityId !== null) {
      const entity = this.#entities.get(entityId)
      if (entity && entity.markAsSelf) {
        entity.markAsSelf()
      }
    }
  }

  /**
   * Create an entity and register it in both global registry and chunk membership.
   * @param {ChunkRegistry} chunkRegistry
   * @param {GlobalEntityId} entityId
   * @param {EntityType} entityType
   * @param {ChunkId} chunkId
   * @returns {BaseEntity|null} The created entity, or null if unknown type
   */
  createEntity(chunkRegistry, entityId, entityType, chunkId) {
    console.debug('[EntityRegistry] Creating entity:', { entityId, entityType })
    let entity = null
    if (entityType === EntityType.SHEEP) {
      entity = new SheepEntity(entityId, this.scene)
    } else if (entityType === EntityType.GOBLIN) {
      entity = new GoblinEntity(entityId, this.scene)
    } else if (entityType === EntityType.PLAYER || entityType === EntityType.GHOST_PLAYER) {
      const isSelf = entityId === this.selfEntityId
      entity = new PlayerEntity(entityId, entityType, this.scene, isSelf)
    } else {
      console.error('[EntityRegistry] Unknown entity type:', entityType, 'for entity', entityId)
      return null
    }
    this.#entities.set(entityId, entity)
    entity.chunkId = chunkId
    const chunk = chunkRegistry.getOrCreate(chunkId)
    chunk.entities.add(entityId)
    return entity
  }

  /**
   * Get an entity by its global ID.
   * @param {GlobalEntityId} globalId
   * @returns {BaseEntity|undefined}
   */
  get(globalId) {
    return this.#entities.get(globalId)
  }

  /**
   * Check if an entity exists.
   * @param {GlobalEntityId} globalId
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
   * @param {GlobalEntityId} entityId
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
}
