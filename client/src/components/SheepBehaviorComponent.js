// @ts-check

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class SheepBehaviorComponent
 * @description Client-side mirror of server sheep behavior state.
 */
export class SheepBehaviorComponent {
  /** @type {number} 0 = IDLE, 1 = WALKING */
  state = 0

  /**
   * @param {BufReader} reader
   */
  deserialize(reader) {
    this.state = reader.readUint8()
  }
}
