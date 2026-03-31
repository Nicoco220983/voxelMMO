// @ts-check

/**
 * @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar
 * @typedef {import('../systems/VoxelHighlightSystem.js').VoxelHighlightSystem} VoxelHighlightSystem
 * @typedef {import('../GameClient.js').GameClient} GameClient
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
   * True when in builder mode (movement controls highlighted voxel).
   * @type {boolean}
   */
  builderMode = false

  /**
   * True for one frame when builderMode transitions (either direction).
   * @type {boolean}
   */
  builderModeChanged = false

  /**
   * True when in bulk builder mode (select volume, apply to all voxels).
   * @type {boolean}
   */
  bulkBuilderMode = false

  /**
   * True for one frame when bulkBuilderMode transitions.
   * @type {boolean}
   */
  bulkBuilderModeChanged = false

  /**
   * Current phase of bulk selection: 'none' | 'start' (start voxel set, waiting for end).
   * @type {'none' | 'start'}
   */
  bulkPhase = 'none'

  /**
   * Start voxel for bulk selection.
   * @type {{x: number, y: number, z: number} | null}
   */
  bulkStartVoxel = null

  /**
   * Yaw captured at builder mode entry time, used for movement orientation.
   * @type {number}
   */
  entryYaw = 0

  /**
   * Voxel movement delta for this frame (-1, 0, or 1 per axis).
   * Only set when in builder mode.
   * @type {{x: number, y: number, z: number}}
   */
  builderMoveDelta = { x: 0, y: 0, z: 0 }

  /**
   * Current builder target voxel (highlighted position in builder mode).
   * Updated by main.js from VoxelHighlightSystem.
   * @type {{x: number, y: number, z: number} | null}
   */
  builderTarget = null

  /**
   * Sync controller state with hotbar.
   * Checks if current tool supports builder mode, auto-exits if not.
   * Should be called once per frame.
   * @param {Hotbar} hotbar
   */
  sync(hotbar) {
    const currentTool = hotbar.getSelectedSlot().tool
    
    // Auto-exit builder/bulk mode if tool doesn't support it
    if ((this.builderMode || this.bulkBuilderMode) && 
        (!currentTool || !currentTool.supportsBuilderMode())) {
      this.builderMode = false
      this.builderModeChanged = true
      this.bulkBuilderMode = false
      this.bulkBuilderModeChanged = true
      this.bulkPhase = 'none'
      this.bulkStartVoxel = null
    }
  }

  /**
   * Called once per animation frame before reading state.
   * Resets one-shot flags.
   * @param {number} dt - Delta time in seconds.
   */
  update(dt) {
    this.toolActivated = false
    this.selectedSlotIndex = null
    this.builderModeChanged = false
    this.bulkBuilderModeChanged = false
    this.builderMoveDelta = { x: 0, y: 0, z: 0 }
  }

  /**
   * Clean up any DOM elements or event listeners.
   */
  destroy() {
    // Override in subclass
  }

  /**
   * Handle tool activation and send appropriate input to the server.
   * This is the central place where all tool inputs are sent.
   * @param {GameClient} client
   * @param {Hotbar} hotbar
   * @param {VoxelHighlightSystem} highlightSystem
   */
  sendToolInput(client, hotbar, highlightSystem) {
    const currentTool = hotbar.getSelectedSlot().tool
    if (!currentTool) return

    if (this.bulkBuilderMode && currentTool.supportsBuilderMode()) {
      // Bulk builder mode: state machine for start/end selection
      const target = this.builderTarget
      if (!target) return

      if (this.bulkPhase === 'none') {
        // First click: set start voxel
        this.bulkStartVoxel = { x: target.x, y: target.y, z: target.z }
        this.bulkPhase = 'start'
      } else if (this.bulkPhase === 'start') {
        // Second click: send bulk operation and reset
        const inputData = currentTool.serializeBulkInput(
          this.bulkStartVoxel.x,
          this.bulkStartVoxel.y,
          this.bulkStartVoxel.z,
          target.x,
          target.y,
          target.z
        )
        client.sendInput(inputData)
        this.bulkPhase = 'none'
        this.bulkStartVoxel = null
      }
    } else if (this.builderMode && currentTool.supportsBuilderMode()) {
      // Regular builder mode: use builder target
      const target = this.builderTarget
      if (target) {
        const inputData = currentTool.serializeBuilderInput?.(target) ??
          this.#defaultSerializeBuilderInput(currentTool, target)
        if (inputData) {
          client.sendInput(inputData)
        }
      }
    } else {
      // Normal mode: tool handles the click
      const inputData = currentTool.onClick(highlightSystem)
      if (inputData) {
        client.sendInput(inputData)
      }
    }
  }

  /**
   * Default builder mode serialization for tools without serializeBuilderInput.
   * @private
   * @param {import('../tools/Tool.js').Tool} tool
   * @param {{x: number, y: number, z: number}} target
   * @returns {ArrayBuffer|null}
   */
  #defaultSerializeBuilderInput(tool, target) {
    // Create a mock highlight system that returns our target
    const mockHighlight = {
      getHighlightedVoxel: () => target,
      getPlacementVoxel: () => target,
    }
    return tool.onClick(mockHighlight)
  }
}
