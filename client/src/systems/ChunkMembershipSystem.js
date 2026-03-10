// @ts-check
import { EntityRegistry } from '../EntityRegistry.js'
import { ChunkRegistry } from '../ChunkRegistry.js'
import { chunkIdFromSubVoxelPos, chunkIdToString } from '../types.js'

/**
 * @class ChunkMembershipSystem
 * @description Updates entity chunk membership based on their current predicted position.
 * Runs once per tick to detect entities that have crossed chunk boundaries and
 * moves them between chunk entity sets.
 */
export class ChunkMembershipSystem {
  /**
   * Update chunk membership for all entities.
   * Called once per tick by GameClient.updateEntities().
   * @param {EntityRegistry} entityRegistry
   * @param {ChunkRegistry} chunkRegistry
   */
  static update(entityRegistry, chunkRegistry) {
    for (const entity of entityRegistry.all()) {
      this.#detectAndHandleChunkChange(chunkRegistry, entity)
    }
  }

  /**
   * Detect if entity's current position puts it in a different chunk than entity.chunkId.
   * If so, update entity.chunkId and move entity between chunk entity sets.
   * @param {ChunkRegistry} chunkRegistry
   * @param {import('../entities/BaseEntity.js').BaseEntity} entity
   * @private
   */
  static #detectAndHandleChunkChange(chunkRegistry, entity) {
    // Use current predicted position for chunk detection
    const pos = entity.currentPos
    const actualChunkId = chunkIdFromSubVoxelPos(pos.x, pos.y, pos.z)
    
    if (actualChunkId !== entity.chunkId) {
      console.debug('[ChunkMembershipSystem] Entity crossed chunk boundary:', { 
        entityId: entity.id, 
        fromChunk: chunkIdToString(entity.chunkId), 
        toChunk: chunkIdToString(actualChunkId),
        pos: { x: pos.x, y: pos.y, z: pos.z }
      })
      
      // Remove from old chunk's entities
      const oldChunk = chunkRegistry.get(entity.chunkId)
      if (oldChunk) oldChunk.entities.delete(entity.id)
      
      // Update entity's chunk reference
      entity.chunkId = actualChunkId
      
      // Add to new chunk's entities
      const newChunk = chunkRegistry.getOrCreate(actualChunkId)
      newChunk.entities.add(entity.id)
    }
  }
}
