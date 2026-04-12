// @ts-check
import { BaseComponent } from './BaseComponent.js'

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class GoblinBehaviorComponent
 * @extends BaseComponent
 * @description Client-side mirror of server goblin behavior state.
 *
 * States: 0 = IDLE, 1 = WALKING, 2 = CHASE, 3 = ATTACK
 * Note: Uses same dirty bit (AI_BEHAVIOR_BIT) as SheepBehaviorComponent on server.
 */
export class GoblinBehaviorComponent extends BaseComponent {
  /** @type {Readonly<{state: number}>} Default values for CREATE deserialization */
  static DEFAULT = Object.freeze({
    state: 0  // IDLE
  })

  /** @type {number} 0 = IDLE, 1 = WALKING, 2 = CHASE, 3 = ATTACK */
  state = 0

  /**
   * Reset all fields to default values.
   * Called during CREATE_ENTITY deserialization before selective component reading.
   */
  resetToDefaults() {
    this.state = GoblinBehaviorComponent.DEFAULT.state
  }

  /**
   * @param {GoblinBehaviorComponent?} self
   * @param {BufReader} reader
   * @param {number} messageTick Server tick from the chunk message header.
   */
  static deserialize(self, reader, messageTick) {
    const state = reader.readUint8()
    if(self) self.state = state
    if(!self) {
      console.debug('[GoblinBehaviorComponent] discarded')
      return
    }

    self.markUpdated(messageTick)

    console.debug('[GoblinBehaviorComponent] deserialized:', {
      state: self.state,
    })
  }
}
