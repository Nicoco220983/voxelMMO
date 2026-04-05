// @ts-check
import { BaseController } from './BaseController.js'
import { InputButton } from '../NetworkProtocol.js'

/**
 * Keyboard + mouse controller with pointer-lock look.
 * Handles raw input events and movement computation.
 */
export class KeyboardController extends BaseController {
  /** @type {Record<string, boolean>} */
  #keys = { w: false, a: false, s: false, d: false, space: false, shift: false, r: false, f: false }

  /** @type {HTMLElement} */
  #domElement

  /** @type {boolean} */
  #pointerLocked = false

  /** @type {Function} */
  #boundKeyDown
  /** @type {Function} */
  #boundKeyUp
  /** @type {Function} */
  #boundMouseMove
  /** @type {Function} */
  #boundMouseDown
  /** @type {Function} */
  #boundMouseUp
  /** @type {Function} */
  #boundClick
  /** @type {Function} */
  #boundPointerLockChange

  // Triple-press detection for bulk builder mode
  /** @type {number[]} Timestamps of recent presses on the same slot */
  #pressSequence = []
  /** @type {number} Milliseconds window for press sequence (600ms for triple) */
  static PRESS_WINDOW_MS = 600
  /** @type {number|null} Slot index of pending tool key press */
  #pendingToolSlot = null
  /** @type {number} Number of presses in current sequence */
  #pendingPressCount = 0

  // Track which builder-move keys have been processed (for per-keypress movement)
  /** @type {Record<string, boolean>} */
  #builderKeyProcessed = { w: false, a: false, s: false, d: false, space: false, shift: false }

  /**
   * @param {HTMLElement} domElement - Element to capture pointer lock on click.
   * @param {Object} options
   * @param {import('../ui/ToolContext.js').ToolContext} options.toolContext - Tool context for dependency access
   * @param {import('../ui/Hotbar.js').Hotbar} options.hotbar - Hotbar UI component
   */
  /** @type {Function} */
  #boundWindowKeyDown

  constructor(domElement, { toolContext, hotbar }) {
    super({ toolContext, hotbar })
    this.yaw = 0
    this.pitch = -0.3
    this.#domElement = domElement

    this.#boundKeyDown = this.#onKeyDown.bind(this)
    this.#boundKeyUp = this.#onKeyUp.bind(this)
    this.#boundMouseMove = this.#onMouseMove.bind(this)
    this.#boundMouseDown = this.#onMouseDown.bind(this)
    this.#boundMouseUp = this.#onMouseUp.bind(this)
    this.#boundClick = this.#onClick.bind(this)
    this.#boundPointerLockChange = this.#onPointerLockChange.bind(this)
    this.#boundWindowKeyDown = this.#onWindowKeyDown.bind(this)

    /** @type {boolean} True when pointer lock was just released (used for ESC handling) */
    this.pointerLockJustExited = false
    /** @type {boolean} True when Q key pressed to unselect tool without exiting pointer lock */
    this.unselectToolPressed = false

    window.addEventListener('keydown', this.#boundKeyDown)
    window.addEventListener('keyup', this.#boundKeyUp)
    document.addEventListener('mousemove', this.#boundMouseMove)
    window.addEventListener('mousedown', this.#boundMouseDown)
    window.addEventListener('mouseup', this.#boundMouseUp)
    this.#domElement.addEventListener('click', this.#boundClick)
    document.addEventListener('pointerlockchange', this.#boundPointerLockChange)
    
    // Global keyboard handler for hotbar (from main.js)
    window.addEventListener('keydown', this.#boundWindowKeyDown)
  }

  /**
   * Global keydown handler for hotbar number keys.
   * Separated from #onKeyDown to avoid conflicts with pointer lock logic.
   * Note: Q key handling is done in #onKeyDown via unselectToolPressed flag + sync().
   * @param {KeyboardEvent} e
   */
  #onWindowKeyDown(e) {
    // Note: Q key is handled in #onKeyDown which sets unselectToolPressed flag.
    // The actual unselection happens in BaseController.sync() -> #syncToolUnselection().
    // This ensures proper timing with the render loop.

    if (this.hotbar.handleKeyDown(e)) {
      // Hotbar handled it, sync selection state if needed
      const slot = this.hotbar.getSelectedSlot()
      if (slot.index < 0) {
        // Tool was unselected
        this.selectedSlotIndex = null
      }
    }
  }

  /**
   * @param {KeyboardEvent} e
   */
  #onKeyDown(e) {
    // Hotbar selection (1-0 keys)
    const toolSlot = this.#keyCodeToSlot(e.code)
    if (toolSlot !== null) {
      const now = performance.now()

      // Clean old presses outside the window
      this.#pressSequence = this.#pressSequence.filter(t => now - t < KeyboardController.PRESS_WINDOW_MS)

      // Check if this is a continuation of the same slot
      if (this.#pendingToolSlot !== null && this.#pendingToolSlot !== toolSlot) {
        // Different slot - reset sequence
        this.#pressSequence = []
        this.#pendingPressCount = 0
      }

      this.#pendingToolSlot = toolSlot
      this.#pressSequence.push(now)
      this.#pendingPressCount = this.#pressSequence.length

      e.preventDefault()
      return
    }

    switch (e.code) {
      case 'KeyW': case 'ArrowUp': this.#keys.w = true; e.preventDefault(); break
      case 'KeyA': case 'ArrowLeft': this.#keys.a = true; e.preventDefault(); break
      case 'KeyS': case 'ArrowDown': this.#keys.s = true; e.preventDefault(); break
      case 'KeyD': case 'ArrowRight': this.#keys.d = true; e.preventDefault(); break
      case 'Space': this.#keys.space = true; e.preventDefault(); break
      case 'ShiftLeft': case 'ShiftRight': this.#keys.shift = true; break
      case 'Escape':
        // ESC handling is now delegated to Hotbar via handleKeyDown
        // We don't handle it here to allow Hotbar to manage tool unselection
        break
      case 'KeyQ':
        // Q key unselects tool without exiting pointer lock
        this.unselectToolPressed = true
        e.preventDefault()
        break
      case 'KeyB':
        // B key toggles builder mode - mark for processing in update()
        this.#toggleBuilderModeRequested = true
        e.preventDefault()
        break
      case 'KeyR':
        // R key rotates around X axis in paste mode
        this.#keys.r = true
        e.preventDefault()
        break
      case 'KeyF':
        // F key rotates around Y axis in paste mode
        this.#keys.f = true
        e.preventDefault()
        break
      default: return
    }
  }

  /**
   * Convert key code to hotbar slot index.
   * @param {string} code
   * @returns {number|null} Slot index 0-9, or null if not a tool key.
   */
  #keyCodeToSlot(code) {
    if (code >= 'Digit1' && code <= 'Digit9') {
      return parseInt(code.slice(5)) - 1
    }
    if (code === 'Digit0') {
      return 9
    }
    if (code >= 'Numpad1' && code <= 'Numpad9') {
      return parseInt(code.slice(6)) - 1
    }
    if (code === 'Numpad0') {
      return 9
    }
    return null
  }

  /**
   * @param {KeyboardEvent} e
   */
  #onKeyUp(e) {
    switch (e.code) {
      case 'KeyW': case 'ArrowUp':
        this.#keys.w = false
        this.#builderKeyProcessed.w = false
        break
      case 'KeyA': case 'ArrowLeft':
        this.#keys.a = false
        this.#builderKeyProcessed.a = false
        break
      case 'KeyS': case 'ArrowDown':
        this.#keys.s = false
        this.#builderKeyProcessed.s = false
        break
      case 'KeyD': case 'ArrowRight':
        this.#keys.d = false
        this.#builderKeyProcessed.d = false
        break
      case 'Space':
        this.#keys.space = false
        this.#builderKeyProcessed.space = false
        break
      case 'ShiftLeft': case 'ShiftRight':
        this.#keys.shift = false
        this.#builderKeyProcessed.shift = false
        break
      case 'KeyR':
        this.#keys.r = false
        break
      case 'KeyF':
        this.#keys.f = false
        break
      default: return
    }
  }

  /** @type {boolean} */
  #toggleBuilderModeRequested = false

  /**
   * @param {MouseEvent} e
   */
  #onMouseMove(e) {
    if (!this.#pointerLocked) return
    const movementX = e.movementX || 0
    const movementY = e.movementY || 0
    this.yaw -= movementX * 0.002
    this.pitch -= movementY * 0.002
    this.pitch = Math.max(-Math.PI / 2 + 0.01, Math.min(Math.PI / 2 - 0.01, this.pitch))
  }

  /**
   * @param {MouseEvent} e
   */
  #onMouseDown(e) {
    if (e.button !== 0) return
    this.actionPressStartTime = performance.now()
    this.actionPressConsumed = false
  }

  /**
   * @param {MouseEvent} e
   */
  #onMouseUp(e) {
    if (e.button !== 0) return
    if (this.actionPressStartTime !== null) {
      if (!this.actionPressConsumed) {
        this.toolActivated = true
      }
      this.actionPressStartTime = null
      this.actionPressConsumed = false
      this.longPressTriggered = false
    }
  }

  #onClick() {
    this.#domElement.requestPointerLock()
  }

  #onPointerLockChange() {
    const wasLocked = this.#pointerLocked
    this.#pointerLocked = document.pointerLockElement === this.#domElement
    // Detect when pointer lock was just released (user pressed ESC)
    if (wasLocked && !this.#pointerLocked) {
      this.pointerLockJustExited = true
    }
  }

  /**
   * Process pending tool key presses and builder mode toggle.
   * Called from main loop with access to all dependencies.
   * @param {import('../ui/VoxelHighlight.js').VoxelHighlight} highlightSystem
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   * @param {import('three').PerspectiveCamera} camera
   */
  processPendingInputs(highlightSystem, chunkRegistry, camera) {
    // Process tool key press
    if (this.#pendingToolSlot !== null) {
      // Cap at 2 presses (double press) - triple press is not used anymore
      const pressCount = Math.min(this.#pendingPressCount, 2)
      this.handleToolKeyPress(
        this.#pendingToolSlot,
        pressCount,
        highlightSystem,
        chunkRegistry,
        camera
      )
      this.#pendingToolSlot = null
      this.#pendingPressCount = 0
    }

    // Process builder mode toggle
    if (this.#toggleBuilderModeRequested) {
      this.toggleBuilderMode(highlightSystem, chunkRegistry, camera)
      this.#toggleBuilderModeRequested = false
    }
  }

  /**
   * Update keyboard controller state.
   * Computes button masks and builder movement deltas.
   * Note: resetFrameState() is called separately from main.js at end of frame.
   * Note: unselectToolPressed is consumed by BaseController.sync() -> #syncToolUnselection()
   * @param {number} dt
   */
  update(dt) {
    // Reset pointer lock exit flag (consumed by main.js before update)
    this.pointerLockJustExited = false
    // Note: unselectToolPressed is NOT reset here - it's consumed in sync() -> #syncToolUnselection()

    let b = 0
    if (this.#keys.w) b |= InputButton.FORWARD
    if (this.#keys.s) b |= InputButton.BACKWARD
    if (this.#keys.a) b |= InputButton.LEFT
    if (this.#keys.d) b |= InputButton.RIGHT
    if (this.#keys.space) b |= InputButton.JUMP
    if (this.#keys.shift) b |= InputButton.DESCEND
    this.buttons = b

    // Compute builder movement delta when in builder mode (per-keypress, not continuous)
    if (this.isBuilderMode()) {
      const delta = this.#computeBuilderMoveDeltaPerKeypress()
      this.setBuilderMoveDelta(delta.x, delta.y, delta.z)
    } else {
      // Reset processed flags when not in builder mode to ensure clean state on entry
      this.#resetBuilderKeyProcessed()
    }

    // Handle rotation input for paste mode
    this.handleRotationInput({
      rotateX: this.#keys.r,
      rotateY: this.#keys.f
    })
  }

  /**
   * Reset all builder key processed flags.
   * @private
   */
  #resetBuilderKeyProcessed() {
    this.#builderKeyProcessed.w = false
    this.#builderKeyProcessed.a = false
    this.#builderKeyProcessed.s = false
    this.#builderKeyProcessed.d = false
    this.#builderKeyProcessed.space = false
    this.#builderKeyProcessed.shift = false
  }

  /**
   * Compute voxel movement delta per keypress (not while holding).
   * Only returns non-zero delta on the first frame a key is pressed.
   * Movement is locked to cardinal axes so W/A/S/D are always distinct.
   * @returns {{x: number, y: number, z: number}}
   */
  #computeBuilderMoveDeltaPerKeypress() {
    const { forward, right } = this.getCardinalBuilderDirections()

    let dx = 0
    let dz = 0
    let dy = 0

    // Forward (W): only move if key is pressed AND not yet processed
    if (this.#keys.w && !this.#builderKeyProcessed.w) {
      dx += forward.x
      dz += forward.z
      this.#builderKeyProcessed.w = true
    }
    // Backward (S)
    if (this.#keys.s && !this.#builderKeyProcessed.s) {
      dx -= forward.x
      dz -= forward.z
      this.#builderKeyProcessed.s = true
    }
    // Right (D)
    if (this.#keys.d && !this.#builderKeyProcessed.d) {
      dx += right.x
      dz += right.z
      this.#builderKeyProcessed.d = true
    }
    // Left (A)
    if (this.#keys.a && !this.#builderKeyProcessed.a) {
      dx -= right.x
      dz -= right.z
      this.#builderKeyProcessed.a = true
    }
    // Up (Space)
    if (this.#keys.space && !this.#builderKeyProcessed.space) {
      dy += 1
      this.#builderKeyProcessed.space = true
    }
    // Down (Shift)
    if (this.#keys.shift && !this.#builderKeyProcessed.shift) {
      dy -= 1
      this.#builderKeyProcessed.shift = true
    }

    return { x: dx, y: dy, z: dz }
  }

  destroy() {
    window.removeEventListener('keydown', this.#boundKeyDown)
    window.removeEventListener('keyup', this.#boundKeyUp)
    document.removeEventListener('mousemove', this.#boundMouseMove)
    window.removeEventListener('mousedown', this.#boundMouseDown)
    window.removeEventListener('mouseup', this.#boundMouseUp)
    this.#domElement.removeEventListener('click', this.#boundClick)
    document.removeEventListener('pointerlockchange', this.#boundPointerLockChange)
    window.removeEventListener('keydown', this.#boundWindowKeyDown)
    if (document.pointerLockElement === this.#domElement) {
      document.exitPointerLock()
    }
  }
}
