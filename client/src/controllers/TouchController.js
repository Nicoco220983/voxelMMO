// @ts-check
import { BaseController } from './BaseController.js'
import { InputButton } from '../NetworkProtocol.js'

/**
 * Touch controller with on-screen joysticks and buttons for smartphone play.
 */
export class TouchController extends BaseController {
  /** @type {HTMLElement|null} */
  #lookBase = null
  /** @type {HTMLElement|null} */
  #lookKnob = null
  /** @type {HTMLElement|null} */
  #moveBase = null
  /** @type {HTMLElement|null} */
  #moveKnob = null
  /** @type {HTMLElement|null} */
  #jumpBtn = null
  /** @type {HTMLElement|null} */
  #descendBtn = null
  /** @type {HTMLElement|null} */
  #actionBtn = null

  /** @type {number|null} */
  #lookTouchId = null
  /** @type {number|null} */
  #moveTouchId = null

  /** @type {number} */
  #lookBaseX = 0
  /** @type {number} */
  #lookBaseY = 0
  /** @type {number} */
  #moveBaseX = 0
  /** @type {number} */
  #moveBaseY = 0

  /** @type {number} */
  #lookDx = 0
  /** @type {number} */
  #lookDy = 0
  /** @type {number} */
  #moveDx = 0
  /** @type {number} */
  #moveDy = 0

  /** @type {boolean} */
  #jumpPressed = false
  /** @type {boolean} */
  #descendPressed = false

  /** @type {number} Joystick radius in CSS pixels. */
  #radius = 40

  /** @type {Function} */
  #boundTouchStart
  /** @type {Function} */
  #boundTouchMove
  /** @type {Function} */
  #boundTouchEnd

  constructor() {
    super()
    this.yaw = 0
    this.pitch = -0.3

    this.#boundTouchStart = this.#onTouchStart.bind(this)
    this.#boundTouchMove = this.#onTouchMove.bind(this)
    this.#boundTouchEnd = this.#onTouchEnd.bind(this)

    this.#buildDOM()
    document.body.classList.add('touch-mode')

    document.addEventListener('touchstart', this.#boundTouchStart, { passive: false })
    document.addEventListener('touchmove', this.#boundTouchMove, { passive: false })
    document.addEventListener('touchend', this.#boundTouchEnd)
    document.addEventListener('touchcancel', this.#boundTouchEnd)
  }

  #buildDOM() {
    const createBase = (cls) => {
      const el = document.createElement('div')
      el.className = `joystick-base ${cls}`
      return el
    }
    const createKnob = () => {
      const el = document.createElement('div')
      el.className = 'joystick-knob'
      return el
    }
    const createBtn = (cls, text) => {
      const el = document.createElement('div')
      el.className = `touch-btn ${cls}`
      el.textContent = text
      return el
    }

    this.#lookBase = createBase('joystick-look')
    this.#lookKnob = createKnob()
    this.#lookBase.appendChild(this.#lookKnob)

    this.#moveBase = createBase('joystick-move')
    this.#moveKnob = createKnob()
    this.#moveBase.appendChild(this.#moveKnob)

    this.#jumpBtn = createBtn('jump-btn', '▲')
    this.#descendBtn = createBtn('descend-btn', '▼')
    this.#actionBtn = createBtn('action-btn', '●')

    document.body.appendChild(this.#lookBase)
    document.body.appendChild(this.#moveBase)
    document.body.appendChild(this.#jumpBtn)
    document.body.appendChild(this.#descendBtn)
    document.body.appendChild(this.#actionBtn)

    // Position after append so we can read rects if needed; we use fixed CSS positions.
    this.#updateKnob(this.#lookKnob, 0, 0)
    this.#updateKnob(this.#moveKnob, 0, 0)
  }

  /**
   * @param {HTMLElement} knob
   * @param {number} dx
   * @param {number} dy
   */
  #updateKnob(knob, dx, dy) {
    knob.style.transform = `translate(${dx}px, ${dy}px)`
  }

  /**
   * @param {TouchEvent} e
   */
  #onTouchStart(e) {
    for (const touch of e.changedTouches) {
      const target = /** @type {HTMLElement|null} */ (touch.target)
      if (!target) continue

      if (target === this.#actionBtn || this.#actionBtn?.contains(target)) {
        this.toolActivated = true
        e.preventDefault()
        continue
      }

      if (target === this.#jumpBtn || this.#jumpBtn?.contains(target)) {
        this.#jumpPressed = true
        e.preventDefault()
        continue
      }

      if (target === this.#descendBtn || this.#descendBtn?.contains(target)) {
        this.#descendPressed = true
        e.preventDefault()
        continue
      }

      if ((target === this.#lookBase || this.#lookBase?.contains(target)) && this.#lookTouchId === null) {
        this.#lookTouchId = touch.identifier
        const rect = this.#lookBase.getBoundingClientRect()
        this.#lookBaseX = rect.left + rect.width / 2
        this.#lookBaseY = rect.top + rect.height / 2
        this.#updateLook(touch.clientX, touch.clientY)
        e.preventDefault()
        continue
      }

      if ((target === this.#moveBase || this.#moveBase?.contains(target)) && this.#moveTouchId === null) {
        this.#moveTouchId = touch.identifier
        const rect = this.#moveBase.getBoundingClientRect()
        this.#moveBaseX = rect.left + rect.width / 2
        this.#moveBaseY = rect.top + rect.height / 2
        this.#updateMove(touch.clientX, touch.clientY)
        e.preventDefault()
        continue
      }
    }
  }

  /**
   * @param {TouchEvent} e
   */
  #onTouchMove(e) {
    for (const touch of e.changedTouches) {
      if (touch.identifier === this.#lookTouchId) {
        this.#updateLook(touch.clientX, touch.clientY)
        e.preventDefault()
      } else if (touch.identifier === this.#moveTouchId) {
        this.#updateMove(touch.clientX, touch.clientY)
        e.preventDefault()
      }
    }
  }

  /**
   * @param {TouchEvent} e
   */
  #onTouchEnd(e) {
    for (const touch of e.changedTouches) {
      if (touch.identifier === this.#lookTouchId) {
        this.#lookTouchId = null
        this.#lookDx = 0
        this.#lookDy = 0
        this.#updateKnob(this.#lookKnob, 0, 0)
      } else if (touch.identifier === this.#moveTouchId) {
        this.#moveTouchId = null
        this.#moveDx = 0
        this.#moveDy = 0
        this.#updateKnob(this.#moveKnob, 0, 0)
      } else {
        const target = /** @type {HTMLElement|null} */ (touch.target)
        if (target === this.#jumpBtn || this.#jumpBtn?.contains(target)) {
          this.#jumpPressed = false
        }
        if (target === this.#descendBtn || this.#descendBtn?.contains(target)) {
          this.#descendPressed = false
        }
      }
    }
  }

  /**
   * @param {number} clientX
   * @param {number} clientY
   */
  #updateLook(clientX, clientY) {
    const dxRaw = clientX - this.#lookBaseX
    const dyRaw = clientY - this.#lookBaseY
    const dist = Math.hypot(dxRaw, dyRaw)
    const clamped = Math.min(dist, this.#radius)
    const angle = Math.atan2(dyRaw, dxRaw)
    this.#lookDx = (Math.cos(angle) * clamped) / this.#radius
    this.#lookDy = (Math.sin(angle) * clamped) / this.#radius
    this.#updateKnob(this.#lookKnob, this.#lookDx * this.#radius, this.#lookDy * this.#radius)
  }

  /**
   * @param {number} clientX
   * @param {number} clientY
   */
  #updateMove(clientX, clientY) {
    const dxRaw = clientX - this.#moveBaseX
    const dyRaw = clientY - this.#moveBaseY
    const dist = Math.hypot(dxRaw, dyRaw)
    const clamped = Math.min(dist, this.#radius)
    const angle = Math.atan2(dyRaw, dxRaw)
    this.#moveDx = (Math.cos(angle) * clamped) / this.#radius
    this.#moveDy = (Math.sin(angle) * clamped) / this.#radius
    this.#updateKnob(this.#moveKnob, this.#moveDx * this.#radius, this.#moveDy * this.#radius)
  }

  /**
   * @param {number} dt
   */
  update(dt) {
    super.update(dt)

    // Look stick drives yaw/pitch continuously while held.
    const lookSpeed = 2.0 // radians per second at full deflection
    this.yaw -= this.#lookDx * lookSpeed * dt
    this.pitch -= this.#lookDy * lookSpeed * dt
    this.pitch = Math.max(-Math.PI / 2 + 0.01, Math.min(Math.PI / 2 - 0.01, this.pitch))

    // Move stick maps to buttons.
    const deadZone = 0.15
    let b = 0
    if (this.#moveDy < -deadZone) b |= InputButton.FORWARD
    if (this.#moveDy > deadZone) b |= InputButton.BACKWARD
    if (this.#moveDx < -deadZone) b |= InputButton.LEFT
    if (this.#moveDx > deadZone) b |= InputButton.RIGHT
    if (this.#jumpPressed) b |= InputButton.JUMP
    if (this.#descendPressed) b |= InputButton.DESCEND
    this.buttons = b
  }

  destroy() {
    document.removeEventListener('touchstart', this.#boundTouchStart)
    document.removeEventListener('touchmove', this.#boundTouchMove)
    document.removeEventListener('touchend', this.#boundTouchEnd)
    document.removeEventListener('touchcancel', this.#boundTouchEnd)
    document.body.classList.remove('touch-mode')
    this.#lookBase?.remove()
    this.#moveBase?.remove()
    this.#jumpBtn?.remove()
    this.#descendBtn?.remove()
    this.#actionBtn?.remove()
  }
}
