// @ts-check
import { BaseComponent } from './BaseComponent.js'

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class SheepBehaviorComponent
 * @extends BaseComponent
 * @description Client-side mirror of server sheep behavior state.
 */
export class SheepBehaviorComponent extends BaseComponent {
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
   * @param {SheepBehaviorComponent?} self
   * @param {BufReader} reader
   * @param {number} messageTick Server tick from the chunk message header.
   */
  static deserialize(self, reader, messageTick) {
    const state = reader.readUint8()
    if(self) self.state = state
    if(!self) {
      console.debug('[SheepBehaviorComponent] discarded')
      return
    }

    self.markUpdated(messageTick)

    console.debug('[SheepBehaviorComponent] deserialized:', {
      state: self.state,
    })
  }
}
