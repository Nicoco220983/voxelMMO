// @ts-check
import { BaseController } from './BaseController.js'
import { InputButton } from '../NetworkProtocol.js'

/**
 * Keyboard + mouse controller with pointer-lock look.
 */
export class KeyboardController extends BaseController {
  /** @type {Record<string, boolean>} */
  #keys = { w: false, a: false, s: false, d: false, space: false, shift: false }

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
  #boundClick
  /** @type {Function} */
  #boundPointerLockChange

  // Triple-press detection for bulk builder mode
  /** @type {number[]} Timestamps of recent presses on the same slot */
  #pressSequence = []
  /** @type {number} Milliseconds window for press sequence (600ms for triple) */
  static PRESS_WINDOW_MS = 600

  // Track which builder-move keys have been processed (for per-keypress movement)
  /** @type {Record<string, boolean>} */
  #builderKeyProcessed = { w: false, a: false, s: false, d: false, space: false, shift: false }

  /**
   * @param {HTMLElement} [domElement] - Element to capture pointer lock on click.
   */
  constructor(domElement = document.body) {
    super()
    this.yaw = 0
    this.pitch = -0.3
    this.#domElement = domElement

    this.#boundKeyDown = this.#onKeyDown.bind(this)
    this.#boundKeyUp = this.#onKeyUp.bind(this)
    this.#boundMouseMove = this.#onMouseMove.bind(this)
    this.#boundMouseDown = this.#onMouseDown.bind(this)
    this.#boundClick = this.#onClick.bind(this)
    this.#boundPointerLockChange = this.#onPointerLockChange.bind(this)

    window.addEventListener('keydown', this.#boundKeyDown)
    window.addEventListener('keyup', this.#boundKeyUp)
    document.addEventListener('mousemove', this.#boundMouseMove)
    window.addEventListener('mousedown', this.#boundMouseDown)
    this.#domElement.addEventListener('click', this.#boundClick)
    document.addEventListener('pointerlockchange', this.#boundPointerLockChange)
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
      this.#pressSequence.push(now)
      
      const pressCount = this.#pressSequence.length
      
      if (pressCount >= 3) {
        // Triple press -> bulk builder mode
        this.bulkBuilderMode = true
        this.bulkBuilderModeChanged = true
        this.builderMode = true
        this.builderModeChanged = !this.builderMode // true if was not in builder
        this.entryYaw = this.yaw
        // Reset phase for new bulk operation
        this.bulkPhase = 'none'
        this.bulkStartVoxel = null
        this.#pressSequence = [] // Reset sequence
      } else if (pressCount === 2) {
        // Double press -> builder mode
        const oldBulkMode = this.bulkBuilderMode
        this.bulkBuilderMode = false
        this.bulkBuilderModeChanged = oldBulkMode !== false
        if (!this.builderMode) {
          this.builderMode = true
          this.builderModeChanged = true
          this.entryYaw = this.yaw
        }
        // Reset bulk state
        this.bulkPhase = 'none'
        this.bulkStartVoxel = null
      } else {
        // Single press -> normal mode
        const oldBuilderMode = this.builderMode
        const oldBulkMode = this.bulkBuilderMode
        this.builderMode = false
        this.builderModeChanged = oldBuilderMode !== false
        this.bulkBuilderMode = false
        this.bulkBuilderModeChanged = oldBulkMode !== false
        // Reset bulk state
        this.bulkPhase = 'none'
        this.bulkStartVoxel = null
      }
      
      this.selectedSlotIndex = toolSlot
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
      default: return
    }
  }

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
    this.toolActivated = true
  }

  #onClick() {
    this.#domElement.requestPointerLock()
  }

  #onPointerLockChange() {
    this.#pointerLocked = document.pointerLockElement === this.#domElement
  }

  update(dt) {
    super.update(dt)
    let b = 0
    if (this.#keys.w) b |= InputButton.FORWARD
    if (this.#keys.s) b |= InputButton.BACKWARD
    if (this.#keys.a) b |= InputButton.LEFT
    if (this.#keys.d) b |= InputButton.RIGHT
    if (this.#keys.space) b |= InputButton.JUMP
    if (this.#keys.shift) b |= InputButton.DESCEND
    this.buttons = b

    // Compute builder movement delta when in builder mode (per-keypress, not continuous)
    if (this.builderMode) {
      this.builderMoveDelta = this.#computeBuilderMoveDeltaPerKeypress()
    } else {
      // Reset processed flags when not in builder mode to ensure clean state on entry
      this.#resetBuilderKeyProcessed()
    }
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
   * @returns {{x: number, y: number, z: number}}
   */
  #computeBuilderMoveDeltaPerKeypress() {
    const cos = Math.cos(this.entryYaw)
    const sin = Math.sin(this.entryYaw)

    let dx = 0
    let dz = 0
    let dy = 0

    // Forward (W): only move if key is pressed AND not yet processed
    if (this.#keys.w && !this.#builderKeyProcessed.w) {
      dx -= Math.round(sin)
      dz -= Math.round(cos)
      this.#builderKeyProcessed.w = true
    }
    // Backward (S)
    if (this.#keys.s && !this.#builderKeyProcessed.s) {
      dx += Math.round(sin)
      dz += Math.round(cos)
      this.#builderKeyProcessed.s = true
    }
    // Right (D)
    if (this.#keys.d && !this.#builderKeyProcessed.d) {
      dx += Math.round(cos)
      dz -= Math.round(sin)
      this.#builderKeyProcessed.d = true
    }
    // Left (A)
    if (this.#keys.a && !this.#builderKeyProcessed.a) {
      dx -= Math.round(cos)
      dz += Math.round(sin)
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
    this.#domElement.removeEventListener('click', this.#boundClick)
    document.removeEventListener('pointerlockchange', this.#boundPointerLockChange)
    if (document.pointerLockElement === this.#domElement) {
      document.exitPointerLock()
    }
  }
}
