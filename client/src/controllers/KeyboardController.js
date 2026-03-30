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
    if (e.code >= 'Digit1' && e.code <= 'Digit9') {
      this.selectedSlotIndex = parseInt(e.code.slice(5)) - 1
      e.preventDefault()
      return
    }
    if (e.code === 'Digit0') {
      this.selectedSlotIndex = 9
      e.preventDefault()
      return
    }
    if (e.code >= 'Numpad1' && e.code <= 'Numpad9') {
      this.selectedSlotIndex = parseInt(e.code.slice(6)) - 1
      e.preventDefault()
      return
    }
    if (e.code === 'Numpad0') {
      this.selectedSlotIndex = 9
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
   * @param {KeyboardEvent} e
   */
  #onKeyUp(e) {
    switch (e.code) {
      case 'KeyW': case 'ArrowUp': this.#keys.w = false; break
      case 'KeyA': case 'ArrowLeft': this.#keys.a = false; break
      case 'KeyS': case 'ArrowDown': this.#keys.s = false; break
      case 'KeyD': case 'ArrowRight': this.#keys.d = false; break
      case 'Space': this.#keys.space = false; break
      case 'ShiftLeft': case 'ShiftRight': this.#keys.shift = false; break
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
