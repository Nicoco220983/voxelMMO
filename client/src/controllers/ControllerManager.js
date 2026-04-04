// @ts-check
import { KeyboardController } from './KeyboardController.js'
import { TouchController } from './TouchController.js'

/** @typedef {import('../ui/ToolContext.js').ToolContext} ToolContext */
/** @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar */

/**
 * Detect the appropriate controller for the current device.
 * @param {HTMLElement} domElement - Element for pointer-lock (keyboard mode only).
 * @param {Object} options
 * @param {ToolContext} options.toolContext - Tool context for dependency access
 * @param {Hotbar} options.hotbar - Hotbar UI component
 * @returns {KeyboardController|TouchController}
 */
export function createController(domElement, { toolContext, hotbar }) {
  const isTouch = 'ontouchstart' in window || navigator.maxTouchPoints > 0
  return isTouch 
    ? new TouchController({ toolContext, hotbar }) 
    : new KeyboardController(domElement, { toolContext, hotbar })
}
