// @ts-check
import { BaseComponent } from './BaseComponent.js'

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class ToolComponent
 * @extends BaseComponent
 * @description Tracks the tool an entity is holding. Mirrors server ToolComponent.
 */
export class ToolComponent extends BaseComponent {
  /** @type {Readonly<ToolComponent>} Default values for CREATE deserialization */
  static DEFAULT = Object.freeze({
    toolId: 0,         // HAND
    lastUsedTick: 0,
  })

  /** @type {number} Tool type ID */
  toolId = 0

  /** @type {number} Server tick when tool was last used */
  lastUsedTick = 0

  /**
   * Reset all fields to default values.
   */
  resetToDefaults() {
    this.toolId = ToolComponent.DEFAULT.toolId
    this.lastUsedTick = ToolComponent.DEFAULT.lastUsedTick
  }

  /**
   * Deserialize from reader: toolId(u8) | lastUsedTick(u32)
   * @param {ToolComponent?} self
   * @param {BufReader} reader
   * @param {number} messageTick
   */
  static deserialize(self, reader, messageTick) {
    const toolId = reader.readUint8()
    const lastUsedTick = reader.readUint32()

    if (!self) {
      console.debug('[ToolComponent] discarded')
      return
    }

    self.toolId = toolId
    self.lastUsedTick = lastUsedTick
    self.markUpdated(messageTick)

    console.debug('[ToolComponent] deserialized:', {
      toolId: self.toolId,
      lastUsedTick: self.lastUsedTick,
    })
  }
}
