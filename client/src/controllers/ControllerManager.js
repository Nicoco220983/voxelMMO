// @ts-check
import { KeyboardController } from './KeyboardController.js'
import { TouchController } from './TouchController.js'

/**
 * Detect the appropriate controller for the current device.
 * @param {HTMLElement} [domElement] - Element for pointer-lock (keyboard mode only).
 * @returns {KeyboardController|TouchController}
 */
export function createController(domElement) {
  const isTouch = 'ontouchstart' in window || navigator.maxTouchPoints > 0
  return isTouch ? new TouchController() : new KeyboardController(domElement)
}
