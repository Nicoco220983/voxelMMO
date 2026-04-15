// @ts-check
import { GameContext } from '../GameContext.js'
import { KeyboardController } from './KeyboardController.js'
import { TouchController } from './TouchController.js'

/** @typedef {import('../ui/VoxelHighlight.js').VoxelHighlight} VoxelHighlight */
/** @typedef {import('../ui/BulkVoxelsSelection.js').BulkVoxelsSelection} BulkVoxelsSelection */
/** @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar */

/**
 * Detect the appropriate controller for the current device.
 * @param {HTMLElement} domElement - Element for pointer-lock (keyboard mode only).
 * @param {Object} options
 * @param {VoxelHighlight} options.voxelHighlight - Voxel highlight system
 * @param {BulkVoxelsSelection} options.bulkSelection - Bulk selection system
 * @param {Hotbar} options.hotbar - Hotbar UI component
 * @returns {KeyboardController|TouchController}
 */
export function createController(domElement, { voxelHighlight, bulkSelection, hotbar }) {
  return GameContext.isMobile
    ? new TouchController({ voxelHighlight, bulkSelection, hotbar })
    : new KeyboardController(domElement, { voxelHighlight, bulkSelection, hotbar })
}
