// @ts-check

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class SheepBehaviorComponent
 * @description Client-side mirror of server sheep behavior state.
 */
export class SheepBehaviorComponent {
  /** @type {Readonly<SheepBehaviorComponent>} Default values for CREATE deserialization */
  static DEFAULT = Object.freeze({
    state: 0  // IDLE
  })

  /** @type {number} 0 = IDLE, 1 = WALKING */
  state = 0

  /**
   * Reset all fields to default values.
   * Called during CREATE_ENTITY deserialization before selective component reading.
   */
  resetToDefaults() {
    this.state = SheepBehaviorComponent.DEFAULT.state
  }

  /**
   * @param {BufReader} reader
   */
  deserialize(reader) {
    this.state = reader.readUint8()
  }
}
