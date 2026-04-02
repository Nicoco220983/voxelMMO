// @ts-check

/**
 * @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar
 * @typedef {import('../ui/VoxelHighlight.js').VoxelHighlight} VoxelHighlight
 * @typedef {import('../GameClient.js').GameClient} GameClient
 */

import { InputType, NetworkProtocol } from '../NetworkProtocol.js'
import { InputModeManager } from './InputModeManager.js'

/**
 * Abstract base class for input controllers (keyboard/mouse or touch).
 * Provides a unified interface so main.js doesn't care about the input source.
 *
 * SIMPLIFIED: Mode state (builder/bulk) is now managed by InputModeManager.
 * Controllers only report raw input events; mode transitions handled centrally.
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
   * Unified mode state manager.
   * @type {InputModeManager}
   */
  modeManager = new InputModeManager()

  /**
   * Set the bulk selection visuals/state manager.
   * @param {import('../ui/BulkVoxelsSelection.js').BulkVoxelsSelection|null} bulkSelection
   */
  setBulkSelection(bulkSelection) {
    this.modeManager.setBulkSelection(bulkSelection)
  }

  /**
   * Threshold in ms for a long press to trigger bulk mode entry.
   * @type {number}
   */
  static LONG_PRESS_MS = 400

  /** @type {number|null} Timestamp when action press started. */
  actionPressStartTime = null

  /** @type {boolean} True for one frame when long-press threshold is crossed. */
  longPressTriggered = false

  /** @type {boolean} True if the current press was already consumed as a long press. */
  actionPressConsumed = false

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
   * Checks if current tool supports builder/bulk mode, auto-exits if not.
   * Should be called once per frame.
   * @param {Hotbar} hotbar
   * @param {VoxelHighlight} highlightSystem
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   * @param {import('three').PerspectiveCamera} camera
   * @param {{x: number, y: number, z: number}|null} currentTarget - Current highlight voxel (for bulk start)
   */
  sync(hotbar, highlightSystem, chunkRegistry, camera, currentTarget) {
    const currentTool = hotbar.getSelectedSlot().tool

    // Auto-exit builder mode if tool doesn't support it
    if (this.modeManager.isBuilderMode() && (!currentTool || !currentTool.supportsBuilderMode())) {
      this.modeManager.exitBuilderMode()
    }

    // Auto-exit bulk mode if tool doesn't support it
    if (this.modeManager.isBulkActive() && (!currentTool || !currentTool.supportsBulkMode())) {
      this.modeManager.exitBulkMode()
      if (this.modeManager.isBuilderMode()) {
        this.modeManager.exitBuilderMode()
      }
    }

    // Handle builder mode voxel movement
    if (this.modeManager.isBuilderMode()) {
      const delta = this.modeManager.builderMoveDelta
      if (delta.x !== 0 || delta.y !== 0 || delta.z !== 0) {
        this.modeManager.moveBuilderTarget(delta.x, delta.y, delta.z)
      }
    }

    // Handle long-press bulk entry (start is always at current highlight voxel)
    this.#checkLongPress()
    if (this.longPressTriggered && currentTool && currentTool.supportsBulkMode()) {
      const mode = currentTool.getHighlightMode()

      if (currentTarget) {
        this.modeManager.enterBulkModeWithStart(currentTarget, mode === 'create' ? 'create' : 'destroy')
      }
      this.longPressTriggered = false
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
    this.longPressTriggered = false
    this.modeManager.update()
  }

  /**
   * Clean up any DOM elements or event listeners.
   */
  destroy() {
    // Override in subclass
  }

  /**
   * Handle tool key press (1-0 keys).
   * Manages mode transitions: single=normal, double=builder, triple=bulk
   * @param {number} slotIndex - Hotbar slot index
   * @param {number} pressCount - Number of presses (1, 2, or 3)
   * @param {Hotbar} hotbar
   * @param {VoxelHighlight} highlightSystem
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   * @param {import('three').PerspectiveCamera} camera
   */
  handleToolKeyPress(slotIndex, pressCount, hotbar, highlightSystem, chunkRegistry, camera) {
    const currentTool = hotbar.getSelectedSlot().tool

    if (pressCount === 1) {
      // Single press: normal mode
      this.modeManager.exitBuilderMode()
    } else if (pressCount >= 2) {
      // Double press: builder mode
      // Note: Triple press no longer triggers bulk mode - use long-press instead
      this.modeManager.exitBulkMode()

      if (currentTool?.supportsBuilderMode()) {
        const mode = currentTool.getHighlightMode()
        const target = highlightSystem.raycastTarget(camera, chunkRegistry, mode)
        this.modeManager.enterBuilderMode(this.yaw, target)
        this.modeManager.exitBulkMode()
      }
    }

    this.selectedSlotIndex = slotIndex
  }

  /**
   * Toggle builder mode on/off (B key).
   * @param {Hotbar} hotbar
   * @param {VoxelHighlight} highlightSystem
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   * @param {import('three').PerspectiveCamera} camera
   */
  toggleBuilderMode(hotbar, highlightSystem, chunkRegistry, camera) {
    const currentTool = hotbar.getSelectedSlot().tool

    if (this.modeManager.isBuilderMode()) {
      // Exit builder mode
      this.modeManager.exitBuilderMode()
    } else {
      // Enter builder mode
      if (currentTool?.supportsBuilderMode()) {
        const mode = currentTool.getHighlightMode()
        const target = highlightSystem.raycastTarget(camera, chunkRegistry, mode)
        this.modeManager.enterBuilderMode(this.yaw, target)
        this.modeManager.exitBulkMode()
      }
    }
  }

  /**
   * Send all pending input to the server.
   * This is the central entry point for all input sending.
   * @param {GameClient} client
   * @param {Hotbar} hotbar
   * @param {VoxelHighlight} highlightSystem
   */
  sendInput(client, hotbar, highlightSystem, camera, chunkRegistry) {
    const currentTool = hotbar.getSelectedSlot().tool
    const mode = currentTool?.getHighlightMode() ?? 'none'

    // Handle tool activation (only if tool selected)
    if (this.toolActivated && currentTool) {
      this.#sendToolInputInternal(client, currentTool, mode, highlightSystem, camera, chunkRegistry)
    }

    // Send movement input only when not in builder mode
    if (!this.modeManager.isBuilderMode()) {
      this.#sendMovementInputInternal(client, hotbar.selectedIndex)
    }
  }

  /**
   * Send tool input based on current tool and mode.
   * @private
   * @param {GameClient} client
   * @param {import('../tools/Tool.js').Tool} tool
   * @param {'destroy'|'create'} mode
   * @param {VoxelHighlight} highlightSystem
   * @param {import('three').PerspectiveCamera} camera
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   */
  #sendToolInputInternal(client, tool, mode, highlightSystem, camera, chunkRegistry) {
    if (this.modeManager.isBulkActive()) {
      // Bulk mode: state machine for start/end selection
      const target = this.modeManager.getBulkTarget(mode, highlightSystem, camera, chunkRegistry)
      if (!target) return

      const result = this.modeManager.onBulkAction(target)

      if (result.complete && result.start && result.end) {
        const inputData = tool.serializeBulkInput(
          result.start.x,
          result.start.y,
          result.start.z,
          result.end.x,
          result.end.y,
          result.end.z
        )
        client.sendInput(inputData)
      }
    } else if (this.modeManager.isBuilderMode()) {
      // Regular builder mode: use builder target
      const target = this.modeManager.getBuilderTarget()
      if (target) {
        const inputData = tool.serializeBuilderInput?.(target) ??
          this.#defaultSerializeBuilderInput(tool, target)
        if (inputData) {
          client.sendInput(inputData)
        }
      }
    } else {
      // Normal mode: tool handles the click
      const inputData = tool.onClick(highlightSystem)
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

  /**
   * Check if long-press threshold has been crossed this frame.
   * @private
   */
  #checkLongPress() {
    if (this.actionPressStartTime !== null && !this.actionPressConsumed) {
      const held = performance.now() - this.actionPressStartTime
      if (held >= BaseController.LONG_PRESS_MS) {
        this.longPressTriggered = true
        this.actionPressConsumed = true
      }
    }
  }

  /**
   * Get current target for visualization.
   * Unified API - doesn't require knowing which mode is active.
   * @param {'destroy'|'create'} toolMode
   * @param {VoxelHighlight} highlightSystem
   * @param {import('three').PerspectiveCamera} camera
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   * @returns {{x: number, y: number, z: number}|null}
   */
  getCurrentTarget(toolMode, highlightSystem, camera, chunkRegistry) {
    return this.modeManager.getTarget(toolMode, highlightSystem, camera, chunkRegistry)
  }

  /**
   * Get current target for bulk selection preview.
   * @param {'destroy'|'create'} toolMode
   * @param {VoxelHighlight} highlightSystem
   * @param {import('three').PerspectiveCamera} camera
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   * @returns {{x: number, y: number, z: number}|null}
   */
  getBulkTarget(toolMode, highlightSystem, camera, chunkRegistry) {
    return this.modeManager.getBulkTarget(toolMode, highlightSystem, camera, chunkRegistry)
  }

  /**
   * Check if currently in builder mode.
   * @returns {boolean}
   */
  get builderMode() {
    return this.modeManager.isBuilderMode()
  }

  /**
   * Check if builder mode just changed this frame.
   * @returns {boolean}
   */
  get builderModeChanged() {
    return this.modeManager.modeChanged
  }
}
