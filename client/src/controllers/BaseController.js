// @ts-check

/**
 * @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar
 * @typedef {import('../ui/VoxelHighlight.js').VoxelHighlight} VoxelHighlight
 * @typedef {import('../ui/BulkVoxelsSelection.js').BulkVoxelsSelection} BulkVoxelsSelection
 * @typedef {import('../GameClient.js').GameClient} GameClient
 * @typedef {import('../ChunkRegistry.js').ChunkRegistry} ChunkRegistry
 */

import { InputType, NetworkProtocol } from '../NetworkProtocol.js'
import { SUBVOXEL_SIZE, PLAYER_EYE_LEVEL } from '../types.js'

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
   * Voxel highlight system for targeting.
   * @type {VoxelHighlight}
   */
  voxelHighlight

  /**
   * Bulk selection system for multi-voxel operations.
   * @type {BulkVoxelsSelection}
   */
  bulkSelection

  /**
   * Hotbar UI component for slot selection.
   * @type {Hotbar}
   */
  hotbar

  /**
   * @param {Object} options
   * @param {VoxelHighlight} options.voxelHighlight - Voxel highlight system
   * @param {BulkVoxelsSelection} options.bulkSelection - Bulk selection system
   * @param {Hotbar} options.hotbar - Hotbar UI component
   */
  constructor({ voxelHighlight, bulkSelection, hotbar }) {
    this.voxelHighlight = voxelHighlight
    this.bulkSelection = bulkSelection
    this.hotbar = hotbar
  }

  /**
   * Flag to identify touch controller (avoids circular import).
   * Override in TouchController to return true.
   * @type {boolean}
   */
  _isTouchController = false

  /**
   * Reference to the game client for accessing player entity and chunks.
   * Set by main.js after client creation.
   * @type {GameClient|null}
   */
  gameClient = null

  /**
   * Set the game client reference.
   * @param {GameClient} client
   */
  setGameClient(client) {
    this.gameClient = client
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
  #lastSentYaw = NaN
  /** @type {number} Last sent pitch */
  #lastSentPitch = NaN
  /** @type {number} Last sent input type */
  #lastInputType = -1



  // ── Mode management ──────────────────────────────────────────────────────

  /**
   * Check if currently in builder mode.
   * @returns {boolean}
   */
  isBuilderMode() {
    return this.voxelHighlight?.isBuilderMode() ?? false
  }

  /**
   * Check if bulk mode is active (any phase).
   * @returns {boolean}
   */
  isBulkActive() {
    return this.bulkSelection.isActive()
  }

  /**
   * Get builder target position from VoxelHighlight.
   * @returns {{x: number, y: number, z: number}|null}
   */
  getBuilderTarget() {
    return this.voxelHighlight?.getCurrentTarget() ?? null
  }

  /**
   * Get bulk selection start position.
   * @returns {{x: number, y: number, z: number}|null}
   */
  getBulkStart() {
    return this.bulkSelection.getStartPos()
  }

  /**
   * Enter builder mode.
   * Sets VoxelHighlight to builder mode and initializes target from current raycast position.
   * @param {{x: number, y: number, z: number}|null} initialTarget - Initial target from raycast
   * @returns {boolean} true if mode changed
   */
  enterBuilderMode(initialTarget) {
    if (!this.voxelHighlight || this.voxelHighlight.isBuilderMode()) return false

    this.voxelHighlight.setMode(this.voxelHighlight.constructor.BUILDER)
    this.voxelHighlight.initBuilderTarget(initialTarget)
    this.modeChanged = true
    return true
  }

  /**
   * Exit builder mode (return to normal).
   * Also exits bulk mode if active.
   * @returns {boolean} true if mode changed
   */
  exitBuilderMode() {
    if (!this.voxelHighlight) return false

    const wasBuilder = this.voxelHighlight.isBuilderMode()
    this.voxelHighlight.setMode(this.voxelHighlight.constructor.NORMAL)
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
    this.bulkSelection.activate(color)
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
    this.bulkSelection.start(startPos, color)
    return true
  }

  /**
   * Exit bulk mode (clear all bulk state).
   */
  exitBulkMode() {
    this.bulkSelection.exit()
  }

  /**
   * Handle bulk action click.
   * Delegates to BulkVoxelsSelection state machine.
   * @param {{x: number, y: number, z: number}|null} targetPos
   * @returns {{consumed: boolean, complete: boolean, start: {x: number, y: number, z: number}|null, end: {x: number, y: number, z: number}|null}}
   */
  onBulkAction(targetPos) {
    return this.bulkSelection.onAction(targetPos)
  }

  /**
   * Get cardinal builder directions based on current yaw.
   * Forward is locked to one of the 4 world axes (+/-X or +/-Z).
   * Right is always 90° clockwise from forward.
   * @returns {{forward: {x: number, z: number}, right: {x: number, z: number}}}
   */
  getCardinalBuilderDirections() {
    let normalizedYaw = this.yaw % (2 * Math.PI)
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
   * Set builder movement delta.
   * Called by controllers when in builder mode.
   * @param {number} dx
   * @param {number} dy
   * @param {number} dz
   */
  setBuilderMoveDelta(dx, dy, dz) {
    this.builderMoveDelta = { x: dx, y: dy, z: dz }
  }

  /**
   * Rotation key processed flags (prevents multiple rotations per keypress).
   * @type {{x: boolean, y: boolean}}
   */
  rotationKeyProcessed = { x: false, y: false }

  /**
   * Handle rotation key input.
   * Called by subclasses when rotation keys are pressed.
   * @param {Object} keys - Key states
   * @param {boolean} [keys.rotateX] - Rotate around X axis requested
   * @param {boolean} [keys.rotateY] - Rotate around Y axis requested
   */
  handleRotationInput(keys) {
    const currentTool = this.hotbar.getCurrentTool()
    
    // Only process rotation when in paste mode with a tool that supports it
    if (!currentTool?.isPasteMode || !currentTool.isPasteMode()) {
      this.rotationKeyProcessed = { x: false, y: false }
      return
    }

    // Handle X rotation (R key)
    if (keys.rotateX && !this.rotationKeyProcessed.x) {
      currentTool.rotateX()
      this.rotationKeyProcessed.x = true
    } else if (!keys.rotateX) {
      this.rotationKeyProcessed.x = false
    }

    // Handle Y rotation (F key)
    if (keys.rotateY && !this.rotationKeyProcessed.y) {
      currentTool.rotateY()
      this.rotationKeyProcessed.y = true
    } else if (!keys.rotateY) {
      this.rotationKeyProcessed.y = false
    }
  }

  /**
   * Move builder target by delta (delegates to VoxelHighlight).
   * @param {number} dx
   * @param {number} dy
   * @param {number} dz
   */
  moveBuilderTarget(dx, dy, dz) {
    this.voxelHighlight?.moveBuilderTarget(dx, dy, dz)
  }

  // ── BaseController methods ───────────────────────────────────────────────

  /**
   * Comprehensive sync that handles all controller-related state updates.
   * This is the central entry point called once per frame from main.js.
   * 
   * @param {VoxelHighlight} highlightSystem
   * @param {ChunkRegistry} chunkRegistry
   * @param {import('three').PerspectiveCamera} camera
   * @param {number} dt - Delta time in seconds
   * @returns {{posX: number, posY: number, posZ: number, vposX: number, vposY: number, vposZ: number}} Player position info
   */
  sync(highlightSystem, chunkRegistry, camera, dt) {
    // ── Tool unselection (Q key) ────────────────────────────────────────────
    this.#syncToolUnselection()

    // ── Hotbar selection from controller ────────────────────────────────────
    this.#syncHotbarSelection()

    // ── Get current tool from hotbar ────────────────────────────────────────
    const currentTool = this.hotbar.getCurrentTool()

    // ── Get current target from VoxelHighlight ──────────────────────────────
    const toolMode = currentTool?.getHighlightMode() ?? 'none'
    const toolColor = currentTool?.getHighlightColor() ?? 0xFFFFFF
    const toolSubMode = currentTool?.getMode?.() ?? null
    const currentTarget = highlightSystem.getTarget(camera, chunkRegistry, toolMode, toolSubMode)

    // ── Process pending inputs (tool key presses with mode transitions) ─────
    this.processPendingInputs?.(highlightSystem, chunkRegistry, camera)

    // ── Sync base controller state (auto-exit modes, builder movement, etc.) ─
    this.#syncInternal(highlightSystem, chunkRegistry, camera, currentTarget, currentTool)

    // ── Update voxel highlight visualization ────────────────────────────────
    highlightSystem.setTarget(currentTarget, toolColor, toolMode, toolSubMode)
    currentTool?.update(highlightSystem, this.isBuilderMode(), this.getBuilderTarget(), currentTarget, toolColor)

    // ── Send all pending input (tool activation and/or movement) ────────────
    this.sendInput(this.gameClient, highlightSystem, camera, chunkRegistry)

    // ── Sync bulk selection visuals ─────────────────────────────────────────
    this.bulkSelection.setColor(toolColor)
    if (this.isBulkActive()) {
      const bulkTarget = highlightSystem.getTarget(camera, chunkRegistry, toolMode, toolSubMode)
      this.bulkSelection.updateEnd(bulkTarget)
    }

    // ── Get player position and sync camera ─────────────────────────────────
    return this.#syncCamera(camera, this.yaw, this.pitch)
  }

  /**
   * Internal sync - handles auto-exit modes, builder movement, long-press.
   * @private
   */
  #syncInternal(highlightSystem, chunkRegistry, camera, currentTarget, currentTool) {
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
   * Sync tool unselection (Q key pressed).
   * @private
   */
  #syncToolUnselection() {
    if (this.unselectToolPressed) {
      const currentTool = this.hotbar.getCurrentTool()
      
      // Check if tool has back navigation (e.g., sub-mode exit)
      const handled = currentTool?.onBackNavigation?.()
      
      if (!handled) {
        // No back navigation handled, unselect the tool
        this.hotbar.clearSelection()
      }
      
      this.unselectToolPressed = false
    }
  }

  /**
   * Sync hotbar selection from controller.
   * @private
   */
  #syncHotbarSelection() {
    if (this.selectedSlotIndex !== null && !(this instanceof /** @type {any} */ (import('./TouchController.js').TouchController))) {
      this.hotbar.selectSlot(this.selectedSlotIndex)
    }
  }

  /**
   * Get local player position from entity registry.
   * @private
   * @returns {{posX: number, posY: number, posZ: number, predGrounded: boolean}}
   */
  #getPlayerPosition() {
    const localPlayer = this.gameClient?.selfEntity
    let posX = 32 * SUBVOXEL_SIZE   // 8192 - default spawn X
    let posY = 22 * SUBVOXEL_SIZE   // 5632 - default spawn Y (approx surface + 2)
    let posZ = 32 * SUBVOXEL_SIZE   // 8192 - default spawn Z
    let predGrounded = false

    if (localPlayer) {
      const pos = localPlayer.currentPos
      posX = pos.x
      posY = pos.y
      posZ = pos.z
      predGrounded = localPlayer.motion.receivedGrounded
    }

    return { posX, posY, posZ, predGrounded }
  }

  /**
   * Sync camera position and rotation with player position.
   * @private
   * @param {import('three').PerspectiveCamera} camera
   * @param {number} yaw
   * @param {number} pitch
   * @returns {{posX: number, posY: number, posZ: number, vposX: number, vposY: number, vposZ: number}}
   */
  #syncCamera(camera, yaw, pitch) {
    const { posX, posY, posZ } = this.#getPlayerPosition()
    const eyeY = posY + PLAYER_EYE_LEVEL

    camera.position.set(posX / SUBVOXEL_SIZE, eyeY / SUBVOXEL_SIZE, posZ / SUBVOXEL_SIZE)
    camera.rotation.y = yaw
    camera.rotation.x = pitch

    return {
      posX, posY, posZ,
      vposX: posX / SUBVOXEL_SIZE,
      vposY: eyeY / SUBVOXEL_SIZE,
      vposZ: posZ / SUBVOXEL_SIZE,
    }
  }

  /**
   * Called once per animation frame to update controller state.
   * Override in subclass to compute button masks, movement deltas, etc.
   * @param {number} dt - Delta time in seconds.
   */
  update(dt) {
    // Override in subclass - compute button masks, update yaw/pitch, etc.
  }

  /**
   * Called once per animation frame at the end to reset one-shot flags.
   * Resets: toolActivated, selectedSlotIndex, longPressTriggered, modeChanged, builderMoveDelta.
   * @param {number} dt - Delta time in seconds.
   */
  resetFrameState(dt) {
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
   * @param {VoxelHighlight} highlightSystem
   * @param {ChunkRegistry} chunkRegistry
   * @param {import('three').PerspectiveCamera} camera
   */
  handleToolKeyPress(slotIndex, pressCount, highlightSystem, chunkRegistry, camera) {
    const currentTool = this.hotbar.getCurrentTool()
    const currentSlot = this.hotbar.getSelectedSlot()
    const expandedView = currentTool?.getExpandedView?.()

    // Check if pressing the same slot as current tool with expanded view
    // If so, this is likely using expanded view controls, not requesting builder mode
    const isExpandedViewInteraction = expandedView && 
                                      currentSlot.index === slotIndex && 
                                      slotIndex < expandedView.items.length

    if (pressCount === 1) {
      // Single press: normal mode
      this.exitBuilderMode()
    } else if (pressCount >= 2 && !isExpandedViewInteraction) {
      // Double press: builder mode (only if not using expanded view controls)
      // Note: Triple press no longer triggers bulk mode - use long-press instead
      this.exitBulkMode()

      if (currentTool?.supportsBuilderMode()) {
        const mode = currentTool.getHighlightMode()
        const target = highlightSystem.raycastTarget(camera, chunkRegistry, mode)
        this.enterBuilderMode(target)
        this.exitBulkMode()
      }
    }

    this.selectedSlotIndex = slotIndex
  }

  /**
   * Toggle builder mode on/off (B key).
   * @param {VoxelHighlight} highlightSystem
   * @param {ChunkRegistry} chunkRegistry
   * @param {import('three').PerspectiveCamera} camera
   */
  toggleBuilderMode(highlightSystem, chunkRegistry, camera) {
    const currentTool = this.hotbar.getCurrentTool()

    if (this.isBuilderMode()) {
      // Exit builder mode
      this.exitBuilderMode()
    } else {
      // Enter builder mode
      if (currentTool?.supportsBuilderMode()) {
        const mode = currentTool.getHighlightMode()
        const target = highlightSystem.raycastTarget(camera, chunkRegistry, mode)
        this.enterBuilderMode(target)
        this.exitBulkMode()
      }
    }
  }

  /**
   * Send all pending input to the server.
   * This is the central entry point for all input sending.
   * @param {GameClient} client
   * @param {VoxelHighlight} highlightSystem
   * @param {import('three').PerspectiveCamera} camera
   * @param {ChunkRegistry} chunkRegistry
   */
  sendInput(client, highlightSystem, camera, chunkRegistry) {
    const currentTool = this.hotbar.getCurrentTool()
    const mode = currentTool?.getHighlightMode() ?? 'none'

    // Handle tool activation (only if tool selected)
    if (this.toolActivated && currentTool) {
      this.#sendToolInputInternal(client, currentTool, mode, highlightSystem, camera, chunkRegistry)
    }

    // Send movement input only when not in builder mode
    if (!this.isBuilderMode()) {
      this.#sendMovementInputInternal(client)
    }
  }

  /**
   * Send tool input based on current tool and mode.
   * @private
   * @param {GameClient} client
   * @param {import('../tools/Tool.js').Tool} tool
   * @param {'destroy'|'create'|'select'} mode
   * @param {VoxelHighlight} highlightSystem
   * @param {import('three').PerspectiveCamera} camera
   * @param {ChunkRegistry} chunkRegistry
   */
  #sendToolInputInternal(client, tool, mode, highlightSystem, camera, chunkRegistry) {
    // Handle entity-targeting tools (combat)
    if (tool.targetsEntities && tool.targetsEntities()) {
      const toolType = tool.getToolType()
      if (toolType !== null) {
        client.sendInput(NetworkProtocol.serializeInputToolUse(toolType, this.yaw, this.pitch))
      }
      return
    }

    // Handle voxel-targeting tools
    if (this.isBulkActive()) {
      // Get sub-mode from tool if available (for select tool)
      const subMode = tool?.getMode ? tool.getMode() : null
      // Bulk mode: state machine for start/end selection
      const target = highlightSystem.getTarget(camera, chunkRegistry, mode, subMode)
      if (!target) return

      const result = this.onBulkAction(target)

      if (result.complete && result.start && result.end) {
        // Use onBulkComplete abstraction for custom bulk behavior
        const inputData = tool.onBulkComplete?.(result.start, result.end)
        if (inputData) {
          // Handle both single input and array of inputs (batch)
          if (Array.isArray(inputData)) {
            for (const input of inputData) {
              client.sendInput(input)
            }
          } else {
            client.sendInput(inputData)
          }
        }
      }
    } else if (this.isBuilderMode()) {
      // Regular builder mode: use builder target
      const target = this.getBuilderTarget()
      if (target) {
        const inputData = tool.serializeBuilderInput?.(target) ??
          this.#defaultSerializeBuilderInput(tool, target)
        if (inputData) {
          // Handle both single input and array of inputs (batch)
          if (Array.isArray(inputData)) {
            for (const input of inputData) {
              client.sendInput(input)
            }
          } else {
            client.sendInput(inputData)
          }
        }
      }
    } else {
      // Normal mode: tool handles the click
      const inputData = tool.onClick(highlightSystem)
      if (inputData) {
        // Handle both single input and array of inputs (batch)
        if (Array.isArray(inputData)) {
          for (const input of inputData) {
            client.sendInput(input)
          }
        } else {
          client.sendInput(inputData)
        }
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
      getCurrentTarget: () => target,
    }
    return tool.onClick(mockHighlight)
  }

  /**
   * Send movement INPUT frame only when state changed.
   * @private
   * @param {GameClient} client
   */
  #sendMovementInputInternal(client) {
    if (this.buttons === this.#lastButtons &&
        this.yaw === this.#lastSentYaw &&
        this.pitch === this.#lastSentPitch) {
      return
    }

    client.sendInput(NetworkProtocol.serializeInputMove(this.buttons, this.yaw, this.pitch))
    this.#lastButtons = this.buttons
    this.#lastSentYaw = this.yaw
    this.#lastSentPitch = this.pitch
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
}
