// @ts-check

/**
 * @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar
 */

/**
 * Abstract base class for input controllers (keyboard/mouse or touch).
 * Provides a unified interface so main.js doesn't care about the input source.
 *
 * @abstract
 */
export class BaseController {
  /** @type {number} InputButton bitmask. */
  buttons = 0

  /** @type {number} Yaw angle in radians. */
  yaw = 0

  /** @type {number} Pitch angle in radians. */
  pitch = 0

  /**
   * True for one frame when the player triggers the primary action
   * (left click / action button).
   * @type {boolean}
   */
  toolActivated = false

  /**
   * If non-null, the hotbar slot index to select this frame.
   * @type {number|null}
   */
  selectedSlotIndex = null

  /**
   * Called once per animation frame before reading state.
   * Resets one-shot flags.
   * @param {number} dt - Delta time in seconds.
   */
  update(dt) {
    this.toolActivated = false
    this.selectedSlotIndex = null
  }

  /**
   * Clean up any DOM elements or event listeners.
   */
  destroy() {
    // Override in subclass
  }
}
