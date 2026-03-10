// @ts-check
import { EntityRegistry } from '../EntityRegistry.js'

/**
 * @class PhysicsPredictionSystem
 * @description Updates current predicted positions for all entities based on
 * their last received server state and elapsed ticks.
 * 
 * This system runs once per tick, before rendering, to compute where entities
 * should be displayed. It uses the same kinematic model as the server so
 * prediction matches server simulation between updates.
 */
export class PhysicsPredictionSystem {
  /**
   * Update predicted positions for all entities in the registry.
   * Called once per tick by GameClient.updateEntities().
   * @param {EntityRegistry} entityRegistry
   * @param {number} currentTick
   */
  static update(entityRegistry, currentTick) {
    for (const entity of entityRegistry.all()) {
      entity.motion.updatePrediction(currentTick)
    }
  }
}
