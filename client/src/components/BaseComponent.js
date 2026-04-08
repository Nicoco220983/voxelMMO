// @ts-check

/**
 * @class BaseComponent
 * @abstract
 * @description Base class for all entity components.
 * Provides versioning support to track when component was last updated by server state.
 */
export class BaseComponent {
  /** @type {number} Last tick this component was updated (CREATE or UPDATE state) */
  lastUpdateTick = 0

  /**
   * Mark component as updated at given tick.
   * Called by deserializers after successful deserialize.
   * @param {number} tick
   */
  markUpdated(tick) {
    this.lastUpdateTick = tick
  }
}
