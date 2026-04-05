// @ts-check
import { BaseController } from './BaseController.js'
import { InputButton } from '../NetworkProtocol.js'

/**
 * Touch controller with on-screen joysticks and buttons for smartphone play.
 * Handles raw input events and movement computation.
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
  /** @type {HTMLElement|null} */
  #rotateXBtn = null
  /** @type {HTMLElement|null} */
  #rotateYBtn = null
  /** @type {HTMLElement|null} */
  #backBtn = null

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

  /** @type {number|null} Touch identifier for the action button. */
  #actionTouchId = null
  /** @type {number|null} Touch identifier for rotate X button. */
  #rotateXTouchId = null
  /** @type {number|null} Touch identifier for rotate Y button. */
  #rotateYTouchId = null
  /** @type {number|null} Touch identifier for back button. */
  #backTouchId = null

  /** @type {Function} */
  #boundTouchStart
  /** @type {Function} */
  #boundTouchMove
  /** @type {Function} */
  #boundTouchEnd

  // Triple-tap detection for bulk builder mode
  /** @type {number[]} Timestamps of recent taps on the same slot */
  #tapSequence = []
  /** @type {number} Milliseconds window for tap sequence (600ms for triple) */
  static TAP_WINDOW_MS = 600
  /** @type {number|null} Slot index of pending hotbar tap */
  #pendingHotbarTap = null
  /** @type {number} Number of taps in current sequence */
  #pendingTapCount = 0

  /**
   * @param {Object} options
   * @param {import('../ui/ToolContext.js').ToolContext} options.toolContext - Tool context for dependency access
   * @param {import('../ui/Hotbar.js').Hotbar} options.hotbar - Hotbar UI component
   */
  constructor({ toolContext, hotbar }) {
    super({ toolContext, hotbar })
    this._isTouchController = true
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

    // Configure hotbar for touch interaction
    this.#setupHotbar(hotbar)
  }

  /**
   * Configure hotbar for touch interaction.
   * Overrides selectSlot to handle tap detection.
   * @param {import('../ui/Hotbar.js').Hotbar} hotbar
   * @private
   */
  #setupHotbar(hotbar) {
    // Override selectSlot to handle tap detection
    const originalSelectSlot = hotbar.selectSlot.bind(hotbar)
    hotbar.selectSlot = (index) => {
      this.onHotbarTap(index)
      originalSelectSlot(index)
    }

    // Wire up back button to trigger slot unselection
    hotbar.onToolUnselected = () => {
      this.selectedSlotIndex = null
    }
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
    this.#rotateXBtn = createBtn('rotate-x-btn', '↻X')
    this.#rotateYBtn = createBtn('rotate-y-btn', '↻Y')
    this.#backBtn = createBtn('back-btn', '←')

    // Hide rotation buttons initially
    if (this.#rotateXBtn) this.#rotateXBtn.style.display = 'none'
    if (this.#rotateYBtn) this.#rotateYBtn.style.display = 'none'
    if (this.#backBtn) this.#backBtn.style.display = 'none'

    document.body.appendChild(this.#lookBase)
    document.body.appendChild(this.#moveBase)
    document.body.appendChild(this.#jumpBtn)
    document.body.appendChild(this.#descendBtn)
    document.body.appendChild(this.#actionBtn)
    document.body.appendChild(this.#rotateXBtn)
    document.body.appendChild(this.#rotateYBtn)
    document.body.appendChild(this.#backBtn)

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
        if (this.#actionTouchId === null) {
          this.#actionTouchId = touch.identifier
          this.actionPressStartTime = performance.now()
          this.actionPressConsumed = false
        }
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

      if (target === this.#rotateXBtn || this.#rotateXBtn?.contains(target)) {
        if (this.#rotateXTouchId === null) {
          this.#rotateXTouchId = touch.identifier
          this.#triggerRotateX()
        }
        e.preventDefault()
        continue
      }

      if (target === this.#rotateYBtn || this.#rotateYBtn?.contains(target)) {
        if (this.#rotateYTouchId === null) {
          this.#rotateYTouchId = touch.identifier
          this.#triggerRotateY()
        }
        e.preventDefault()
        continue
      }

      if (target === this.#backBtn || this.#backBtn?.contains(target)) {
        if (this.#backTouchId === null) {
          this.#backTouchId = touch.identifier
          this.#triggerBack()
        }
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
      } else if (touch.identifier === this.#actionTouchId) {
        this.#actionTouchId = null
        if (this.actionPressStartTime !== null && !this.actionPressConsumed) {
          this.toolActivated = true
        }
        this.actionPressStartTime = null
        this.actionPressConsumed = false
        this.longPressTriggered = false
      } else if (touch.identifier === this.#rotateXTouchId) {
        this.#rotateXTouchId = null
      } else if (touch.identifier === this.#rotateYTouchId) {
        this.#rotateYTouchId = null
      } else if (touch.identifier === this.#backTouchId) {
        this.#backTouchId = null
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
   * Called when a hotbar slot is tapped (from Hotbar.js or main.js).
   * Records the tap for processing in processPendingInputs().
   * @param {number} slotIndex
   */
  onHotbarTap(slotIndex) {
    const now = performance.now()

    // Clean old taps outside the window
    this.#tapSequence = this.#tapSequence.filter(t => now - t < TouchController.TAP_WINDOW_MS)

    // Check if this is a continuation of the same slot
    if (this.#pendingHotbarTap !== null && this.#pendingHotbarTap !== slotIndex) {
      // Different slot - reset sequence
      this.#tapSequence = []
    }

    this.#pendingHotbarTap = slotIndex
    this.#tapSequence.push(now)
    this.#pendingTapCount = this.#tapSequence.length
  }

  /**
   * Process pending hotbar taps.
   * Called from main loop with access to all dependencies.
   * @param {import('../ui/VoxelHighlight.js').VoxelHighlight} highlightSystem
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   * @param {import('three').PerspectiveCamera} camera
   */
  processPendingInputs(highlightSystem, chunkRegistry, camera) {
    if (this.#pendingHotbarTap !== null) {
      // Cap at 2 taps (double tap) - triple tap is not used anymore
      const tapCount = Math.min(this.#pendingTapCount, 2)
      this.handleToolKeyPress(
        this.#pendingHotbarTap,
        tapCount,
        highlightSystem,
        chunkRegistry,
        camera
      )
      this.#pendingHotbarTap = null
      this.#pendingTapCount = 0
    }
  }

  /**
   * Compute voxel movement delta from button mask and entry yaw.
   * Movement is locked to cardinal axes so inputs are unambiguous.
   * @param {number} buttons
   * @returns {{x: number, y: number, z: number}}
   */
  #computeBuilderMoveDelta(buttons) {
    const { forward, right } = this.getCardinalBuilderDirections()

    let dx = 0
    let dz = 0

    if (buttons & InputButton.FORWARD) {
      dx += forward.x
      dz += forward.z
    }
    if (buttons & InputButton.BACKWARD) {
      dx -= forward.x
      dz -= forward.z
    }
    if (buttons & InputButton.RIGHT) {
      dx += right.x
      dz += right.z
    }
    if (buttons & InputButton.LEFT) {
      dx -= right.x
      dz -= right.z
    }

    // Up/Down are world-space Y
    let dy = 0
    if (buttons & InputButton.JUMP) dy += 1
    if (buttons & InputButton.DESCEND) dy -= 1

    return { x: dx, y: dy, z: dz }
  }

  /**
   * Trigger ESC/BACK action for touch devices.
   * Called when the hotbar BACK button is pressed.
   */
  triggerEsc() {
    this.escPressed = true
  }

  /**
   * Trigger rotation around X axis.
   * @private
   */
  #triggerRotateX() {
    const currentTool = this.toolContext?.currentTool
    if (currentTool?.rotateX) {
      currentTool.rotateX()
    }
  }

  /**
   * Trigger rotation around Y axis.
   * @private
   */
  #triggerRotateY() {
    const currentTool = this.toolContext?.currentTool
    if (currentTool?.rotateY) {
      currentTool.rotateY()
    }
  }

  /**
   * Trigger back button (Q key equivalent).
   * @private
   */
  #triggerBack() {
    this.hotbar.handleQ()
    this.selectedSlotIndex = null
  }

  /**
   * Update visibility of rotation buttons based on hotbar mode.
   * Called from main loop when mode changes.
   */
  updateRotationButtonsVisibility() {
    const isRotateMode = this.hotbar?.isInRotateMode?.() ?? false
    
    if (this.#rotateXBtn) {
      this.#rotateXBtn.style.display = isRotateMode ? 'flex' : 'none'
    }
    if (this.#rotateYBtn) {
      this.#rotateYBtn.style.display = isRotateMode ? 'flex' : 'none'
    }
    if (this.#backBtn) {
      // Show back button when in any special mode (voxels, select, rotate)
      const showBack = this.hotbar?.isInVoxelMode?.() || 
                       this.hotbar?.isInSelectMode?.() || 
                       isRotateMode
      this.#backBtn.style.display = showBack ? 'flex' : 'none'
    }
  }

  /**
   * Update touch controller state.
   * @param {number} dt
   */
  update(dt) {
    // Update rotation buttons visibility based on current mode
    this.updateRotationButtonsVisibility()

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

    // Compute builder movement delta when in builder mode
    if (this.isBuilderMode()) {
      const delta = this.#computeBuilderMoveDelta(b)
      this.setBuilderMoveDelta(delta.x, delta.y, delta.z)
    }
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
    this.#rotateXBtn?.remove()
    this.#rotateYBtn?.remove()
    this.#backBtn?.remove()
  }
}
