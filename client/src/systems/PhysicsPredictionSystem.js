// @ts-check
import { EntityRegistry } from '../EntityRegistry.js'

/**
 * @class PhysicsPredictionSystem
 * @description Updates current predicted positions for all entities based on
 * their last received server state and elapsed time.
 * 
 * This system runs once per render frame to compute where entities
 * should be displayed. It uses the same kinematic model as the server so
 * prediction matches server simulation between updates.
 * 
 * Supports sub-tick precision: currentTick can be a float (e.g., 150.6)
 * for smooth interpolation between server ticks.
 */
export class PhysicsPredictionSystem {
  /**
   * Update predicted positions for all entities in the registry.
   * Called once per render frame by GameClient.updateEntities().
   * @param {EntityRegistry} entityRegistry
   * @param {number} renderTick - Float tick with sub-tick precision for smooth interpolation
   */
  static update(entityRegistry, renderTick) {
    for (const entity of entityRegistry.all()) {
      entity.motion.updatePrediction(renderTick)
    }
  }
}
