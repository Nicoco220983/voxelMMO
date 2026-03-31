// @ts-check

/**
 * @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar
 * @typedef {import('../systems/VoxelHighlightSystem.js').VoxelHighlightSystem} VoxelHighlightSystem
 * @typedef {import('../GameClient.js').GameClient} GameClient
 */

import { InputType, NetworkProtocol } from '../NetworkProtocol.js'

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

  // ── Movement input state (for delta compression) ─────────────────────────
  /** @type {number} Last sent buttons bitmask */
  #lastButtons = -1
  /** @type {number} Last sent yaw */
  #lastYaw = NaN
  /** @type {number} Last sent pitch */
  #lastPitch = NaN
  /** @type {number} Last sent input type */
  #lastInputType = -1

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

  /**
   * Send all pending input to the server.
   * This is the central entry point for all input sending.
   * Handles both tool activation and movement based on controller state.
   * @param {GameClient} client
   * @param {Hotbar} hotbar
   * @param {VoxelHighlightSystem} highlightSystem
   */
  sendInput(client, hotbar, highlightSystem) {
    // Priority: tool activation takes precedence over movement
    if (this.toolActivated) {
      this.#sendToolInputInternal(client, hotbar, highlightSystem)
    }

    // Send movement input only when not in builder mode
    if (!this.builderMode) {
      this.#sendMovementInputInternal(client, hotbar.selectedIndex)
    }
  }

  /**
   * Send tool input based on current tool and mode.
   * @private
   * @param {GameClient} client
   * @param {Hotbar} hotbar
   * @param {VoxelHighlightSystem} highlightSystem
   */
  #sendToolInputInternal(client, hotbar, highlightSystem) {
    const currentTool = hotbar.getSelectedSlot().tool
    if (!currentTool) return

    if (this.bulkBuilderMode && currentTool.supportsBuilderMode()) {
      // Bulk builder mode: state machine for start/end selection
      const target = highlightSystem.getBuilderTarget()
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
      const target = highlightSystem.getBuilderTarget()
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
   * Send movement INPUT frame only when state changed.
   * @private
   * @param {GameClient} client
   * @param {number} selectedSlotIndex - Current hotbar slot (for input type mapping)
   */
  #sendMovementInputInternal(client, selectedSlotIndex) {
    const inputType = this.#slotToInputType(selectedSlotIndex)

    if (this.buttons === this.#lastButtons &&
        this.yaw === this.#lastYaw &&
        this.pitch === this.#lastPitch &&
        inputType === this.#lastInputType) {
      return
    }

    client.sendInput(NetworkProtocol.serializeInputMove(this.buttons, this.yaw, this.pitch))
    this.#lastButtons = this.buttons
    this.#lastYaw = this.yaw
    this.#lastPitch = this.pitch
    this.#lastInputType = inputType
  }

  /**
   * Map hotbar slot index to InputType value.
   * @private
   * @param {number} slotIndex
   * @returns {number} InputType value
   */
  #slotToInputType(slotIndex) {
    // For now, all inputs are MOVE type
    // Future: different slots may trigger different input types
    return InputType.MOVE
  }
}
