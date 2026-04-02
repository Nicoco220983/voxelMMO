// @ts-check

/**
 * @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar
 * @typedef {import('../ui/VoxelHighlight.js').VoxelHighlight} VoxelHighlight
 * @typedef {import('../GameClient.js').GameClient} GameClient
 * @typedef {import('../ui/BulkVoxelsSelection.js').BulkVoxelsSelection} BulkVoxelsSelection
 */

import { InputType, NetworkProtocol } from '../NetworkProtocol.js'

/**
 * Abstract base class for input controllers (keyboard/mouse or touch).
 * Provides a unified interface so main.js doesn't care about the input source.
 *
 * Manages input modes: NORMAL → BUILDER → BULK transitions and target management.
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

  // ── Mode state (merged from InputModeManager) ─────────────────────────────

  /** Normal mode: raycast-based targeting */
  static NORMAL = 'normal'
  /** Builder mode: keyboard-controlled voxel target */
  static BUILDER = 'builder'

  /** @type {'normal'|'builder'} */
  #mode = BaseController.NORMAL

  /** Player yaw at builder mode entry time (for movement orientation)
   * @type {number}
   */
  #entryYaw = 0

  /** Builder mode target (keyboard-controlled voxel position)
   * @type {{x: number, y: number, z: number}|null}
   */
  #builderTarget = null

  /** Bulk voxel selection state/visual manager
   * @type {BulkVoxelsSelection|null}
   */
  #bulkSelection = null

  /**
   * True for one frame when mode changes (in either direction).
   * @type {boolean}
   */
  modeChanged = false

  /**
   * Builder movement delta for this frame (-1, 0, or 1 per axis).
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
   * Set the bulk selection visuals/state manager.
   * @param {BulkVoxelsSelection|null} bulkSelection
   */
  setBulkSelection(bulkSelection) {
    this.#bulkSelection = bulkSelection
  }

  // ── Mode management methods ──────────────────────────────────────────────

  /**
   * Get current mode.
   * @returns {'normal'|'builder'}
   */
  getMode() {
    return this.#mode
  }

  /**
   * Get current bulk phase.
   * @returns {'idle'|'selecting_start'|'selecting_end'}
   */
  getBulkPhase() {
    return this.#bulkSelection?.getBulkPhase() ?? 'idle'
  }

  /**
   * Check if currently in builder mode.
   * @returns {boolean}
   */
  isBuilderMode() {
    return this.#mode === BaseController.BUILDER
  }

  /**
   * Check if bulk mode is active (any phase).
   * @returns {boolean}
   */
  isBulkActive() {
    return this.#bulkSelection?.isActive() ?? false
  }

  /**
   * Get entry yaw (for builder mode movement orientation).
   * @returns {number}
   */
  getEntryYaw() {
    return this.#entryYaw
  }

  /**
   * Get builder target position.
   * @returns {{x: number, y: number, z: number}|null}
   */
  getBuilderTarget() {
    return this.#builderTarget
  }

  /**
   * Get bulk selection start position.
   * @returns {{x: number, y: number, z: number}|null}
   */
  getBulkStart() {
    return this.#bulkSelection?.getStartPos() ?? null
  }

  /**
   * Enter builder mode.
   * @param {number} entryYaw - Player yaw at entry time
   * @param {{x: number, y: number, z: number}|null} initialTarget - Initial target from raycast
   * @returns {boolean} true if mode changed
   */
  enterBuilderMode(entryYaw, initialTarget) {
    if (this.#mode === BaseController.BUILDER) return false

    this.#mode = BaseController.BUILDER
    this.#entryYaw = entryYaw
    this.#builderTarget = initialTarget
    this.modeChanged = true
    return true
  }

  /**
   * Exit builder mode (return to normal).
   * Also exits bulk mode if active.
   * @returns {boolean} true if mode changed
   */
  exitBuilderMode() {
    const wasBuilder = this.#mode === BaseController.BUILDER
    this.#mode = BaseController.NORMAL
    this.#builderTarget = null
    this.exitBulkMode()

    if (wasBuilder) {
      this.modeChanged = true
      return true
    }
    return false
  }

  /**
   * Activate bulk mode without setting start (triple-tap entry).
   * Can be used from either normal or builder mode.
   * @param {number} [color=0xFF0000] - Hex color value (0xRRGGBB)
   * @returns {boolean} true if activated
   */
  activateBulkMode(color = 0xFF0000) {
    this.#bulkSelection?.activate(color)
    return true
  }

  /**
   * Enter bulk mode and immediately set start position (long-press entry).
   * Can be used from either normal or builder mode.
   * @param {{x: number, y: number, z: number}} startPos
   * @param {number} [color=0xFF0000] - Hex color value (0xRRGGBB)
   * @returns {boolean} true if activated
   */
  enterBulkModeWithStart(startPos, color = 0xFF0000) {
    this.#bulkSelection?.start(startPos, color)
    return true
  }

  /**
   * Exit bulk mode (clear all bulk state).
   */
  exitBulkMode() {
    this.#bulkSelection?.exit()
  }

  /**
   * Handle bulk action click.
   * Delegates to BulkVoxelsSelection state machine.
   * @param {{x: number, y: number, z: number}|null} targetPos
   * @returns {{consumed: boolean, complete: boolean, start: {x: number, y: number, z: number}|null, end: {x: number, y: number, z: number}|null}}
   */
  onBulkAction(targetPos) {
    return this.#bulkSelection?.onAction(targetPos) ?? { consumed: false, complete: false, start: null, end: null }
  }

  /**
   * Move builder target by delta.
   * @param {number} dx
   * @param {number} dy
   * @param {number} dz
   */
  moveBuilderTarget(dx, dy, dz) {
    if (!this.#builderTarget) return
    const adx = Math.abs(dx)
    const ady = Math.abs(dy)
    const adz = Math.abs(dz)
    // Builder mode: only move along one axis at a time. Priority: Y > Z > X.
    if (ady >= adx && ady >= adz) {
      this.#builderTarget.y += Math.sign(dy)
    } else if (adz >= adx && adz >= ady) {
      this.#builderTarget.z += Math.sign(dz)
    } else if (adx > 0) {
      this.#builderTarget.x += Math.sign(dx)
    }
  }

  /**
   * Set builder target directly (e.g., from raycast on mode entry).
   * @param {{x: number, y: number, z: number}|null} target
   */
  setBuilderTarget(target) {
    this.#builderTarget = target
  }

  /**
   * Get cardinal builder directions based on entry yaw.
   * Forward is locked to one of the 4 world axes (+/-X or +/-Z).
   * Right is always 90° clockwise from forward.
   * @returns {{forward: {x: number, z: number}, right: {x: number, z: number}}}
   */
  getCardinalBuilderDirections() {
    const yaw = this.getEntryYaw()
    let normalizedYaw = yaw % (2 * Math.PI)
    if (normalizedYaw < 0) normalizedYaw += 2 * Math.PI

    let fx = 0, fz = 0
    if (normalizedYaw < Math.PI / 4 || normalizedYaw >= 7 * Math.PI / 4) {
      fx = 0; fz = -1   // facing -Z
    } else if (normalizedYaw < 3 * Math.PI / 4) {
      fx = -1; fz = 0   // facing -X
    } else if (normalizedYaw < 5 * Math.PI / 4) {
      fx = 0; fz = 1    // facing +Z
    } else {
      fx = 1; fz = 0    // facing +X
    }

    // Right is 90° clockwise from forward: (-fz, fx)
    const rx = -fz
    const rz = fx

    return { forward: { x: fx, z: fz }, right: { x: rx, z: rz } }
  }

  /**
   * Get the current target based on mode.
   * Unified API - callers don't need to know which mode is active.
   * @param {'destroy'|'create'} toolMode
   * @param {VoxelHighlight} highlightSystem
   * @param {import('three').PerspectiveCamera} camera
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   * @returns {{x: number, y: number, z: number}|null}
   */
  getTarget(toolMode, highlightSystem, camera, chunkRegistry) {
    if (this.#mode === BaseController.BUILDER) {
      return this.#builderTarget
    }

    // Normal mode: do raycast to get target
    return highlightSystem.raycastTarget(camera, chunkRegistry, toolMode)
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
    if (this.#mode === BaseController.BUILDER) {
      return this.#builderTarget
    }

    return highlightSystem.raycastTarget(camera, chunkRegistry, toolMode)
  }

  /**
   * Set builder movement delta.
   * Called by controllers when in builder mode.
   * @param {number} dx
   * @param {number} dy
   * @param {number} dz
   */
  setBuilderMoveDelta(dx, dy, dz) {
    this.builderMoveDelta = { x: dx, y: dy, z: dz }
  }

  // ── BaseController methods ───────────────────────────────────────────────

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
    if (this.isBuilderMode() && (!currentTool || !currentTool.supportsBuilderMode())) {
      this.exitBuilderMode()
    }

    // Auto-exit bulk mode if tool doesn't support it
    if (this.isBulkActive() && (!currentTool || !currentTool.supportsBulkMode())) {
      this.exitBulkMode()
      if (this.isBuilderMode()) {
        this.exitBuilderMode()
      }
    }

    // Handle builder mode voxel movement
    if (this.isBuilderMode()) {
      const delta = this.builderMoveDelta
      if (delta.x !== 0 || delta.y !== 0 || delta.z !== 0) {
        this.moveBuilderTarget(delta.x, delta.y, delta.z)
      }
    }

    // Handle long-press bulk entry (start is always at current highlight voxel)
    this.#checkLongPress()
    if (this.longPressTriggered && currentTool && currentTool.supportsBulkMode()) {
      if (currentTarget) {
        const toolColor = currentTool?.getHighlightColor() ?? 0xFF0000
        this.enterBulkModeWithStart(currentTarget, toolColor)
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
    // Reset mode change flag
    this.modeChanged = false
    this.builderMoveDelta = { x: 0, y: 0, z: 0 }
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
      this.exitBuilderMode()
    } else if (pressCount >= 2) {
      // Double press: builder mode
      // Note: Triple press no longer triggers bulk mode - use long-press instead
      this.exitBulkMode()

      if (currentTool?.supportsBuilderMode()) {
        const mode = currentTool.getHighlightMode()
        const target = highlightSystem.raycastTarget(camera, chunkRegistry, mode)
        this.enterBuilderMode(this.yaw, target)
        this.exitBulkMode()
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

    if (this.isBuilderMode()) {
      // Exit builder mode
      this.exitBuilderMode()
    } else {
      // Enter builder mode
      if (currentTool?.supportsBuilderMode()) {
        const mode = currentTool.getHighlightMode()
        const target = highlightSystem.raycastTarget(camera, chunkRegistry, mode)
        this.enterBuilderMode(this.yaw, target)
        this.exitBulkMode()
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
    if (!this.isBuilderMode()) {
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
    if (this.isBulkActive()) {
      // Bulk mode: state machine for start/end selection
      const target = this.getBulkTarget(mode, highlightSystem, camera, chunkRegistry)
      if (!target) return

      const result = this.onBulkAction(target)

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
    } else if (this.isBuilderMode()) {
      // Regular builder mode: use builder target
      const target = this.getBuilderTarget()
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
    return this.getTarget(toolMode, highlightSystem, camera, chunkRegistry)
  }

  /**
   * Check if currently in builder mode.
   * @returns {boolean}
   */
  get builderMode() {
    return this.isBuilderMode()
  }
}
