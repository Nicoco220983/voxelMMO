// @ts-check

/**
 * @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar
 * @typedef {import('../ui/VoxelHighlight.js').VoxelHighlight} VoxelHighlight
 * @typedef {import('../ui/BulkVoxelsSelection.js').BulkVoxelsSelection} BulkVoxelsSelection
 */

/** @typedef {import('../tools/Tool.js').Tool} Tool */

/**
 * Centralized context for tool-related state and UI components.
 * Holds the current tool, voxel highlight, and bulk selection systems,
 * providing a unified access point for controllers and game systems.
 */
export class ToolContext {
  /** @type {VoxelHighlight|null} */
  #voxelHighlight = null

  /** @type {BulkVoxelsSelection|null} */
  #bulkSelection = null

  /** @type {Tool|null} */
  #currentTool = null

  /**
   * @param {Object} deps
   * @param {VoxelHighlight|null} [deps.voxelHighlight]
   * @param {BulkVoxelsSelection|null} [deps.bulkSelection]
   * @param {Tool|null} [deps.currentTool]
   */
  constructor({ voxelHighlight = null, bulkSelection = null, currentTool = null } = {}) {
    this.#voxelHighlight = voxelHighlight
    this.#bulkSelection = bulkSelection
    this.#currentTool = currentTool
  }

  /**
   * Get the voxel highlight system.
   * @returns {VoxelHighlight|null}
   */
  get voxelHighlight() {
    return this.#voxelHighlight
  }

  /**
   * Set the voxel highlight system.
   * @param {VoxelHighlight} voxelHighlight
   */
  set voxelHighlight(voxelHighlight) {
    this.#voxelHighlight = voxelHighlight
  }

  /**
   * Get the bulk selection system.
   * @returns {BulkVoxelsSelection|null}
   */
  get bulkSelection() {
    return this.#bulkSelection
  }

  /**
   * Set the bulk selection system.
   * @param {BulkVoxelsSelection} bulkSelection
   */
  set bulkSelection(bulkSelection) {
    this.#bulkSelection = bulkSelection
  }

  /**
   * Get the currently selected tool (read-only, use selectTool() to change).
   * @returns {Tool|null}
   */
  get currentTool() {
    return this.#currentTool
  }

  /**
   * Select a new tool, calling onDeselect on the old tool and onSelect on the new one.
   * @param {Tool|null} tool - The tool to select, or null to deselect
   */
  selectTool(tool) {
    if (this.#currentTool === tool) return

    this.#currentTool?.onDeselect?.(this.#voxelHighlight)
    this.#currentTool = tool
    this.#currentTool?.onSelect?.(this.#voxelHighlight)
  }

  /**
   * Check if a tool is currently selected.
   * @returns {boolean}
   */
  hasToolSelected() {
    return this.#currentTool !== null
  }

  /**
   * Get the current highlight mode based on the selected tool.
   * @returns {'destroy'|'create'|'select'|'none'}
   */
  getHighlightMode() {
    return this.#currentTool?.getHighlightMode() ?? 'none'
  }

  /**
   * Get the current highlight color based on the selected tool.
   * @returns {number}
   */
  getHighlightColor() {
    return this.#currentTool?.getHighlightColor() ?? 0
  }

  /**
   * Get the current tool sub-mode (for tools with modes like SelectVoxelTool).
   * @returns {string|null}
   */
  getToolMode() {
    return this.#currentTool?.getMode?.() ?? null
  }

  /**
   * Check if the current tool supports builder mode.
   * @returns {boolean}
   */
  supportsBuilderMode() {
    return this.#currentTool?.supportsBuilderMode() ?? false
  }

  /**
   * Check if the current tool supports bulk mode.
   * @returns {boolean}
   */
  supportsBulkMode() {
    return this.#currentTool?.supportsBulkMode() ?? false
  }
}
