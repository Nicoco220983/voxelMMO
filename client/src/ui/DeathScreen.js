// @ts-check

/**
 * @class DeathScreen
 * @description UI overlay shown when the player dies. Provides respawn functionality.
 */
export class DeathScreen {
  /** @type {HTMLElement|null} */
  #container = null

  /** @type {HTMLElement|null} */
  #overlay = null

  /** @type {Function|null} */
  #onRespawnCallback = null

  /** @type {boolean} */
  #isVisible = false

  /**
   * @param {Function} onRespawnCallback Called when player clicks respawn button
   */
  constructor(onRespawnCallback) {
    this.#onRespawnCallback = onRespawnCallback
  }

  /**
   * Check if death screen is currently visible.
   * @returns {boolean}
   */
  get isVisible() {
    return this.#isVisible
  }

  /**
   * Show the death screen.
   */
  show() {
    if (this.#isVisible) return
    this.#isVisible = true

    // Create overlay if it doesn't exist
    if (!this.#overlay) {
      this.#createOverlay()
    }

    // Show the overlay
    if (this.#overlay) {
      this.#overlay.style.display = 'flex'
    }

    console.info('[DeathScreen] Player died - showing respawn screen')
  }

  /**
   * Hide the death screen.
   */
  hide() {
    if (!this.#isVisible) return
    this.#isVisible = false

    if (this.#overlay) {
      this.#overlay.style.display = 'none'
    }

    console.info('[DeathScreen] Hiding respawn screen')
  }

  /**
   * Clean up and remove the death screen from DOM.
   */
  destroy() {
    this.hide()
    if (this.#overlay && this.#overlay.parentNode) {
      this.#overlay.parentNode.removeChild(this.#overlay)
    }
    this.#overlay = null
    this.#container = null
    this.#onRespawnCallback = null
  }

  /**
   * Create the death screen overlay DOM elements.
   * @private
   */
  #createOverlay() {
    // Create overlay container
    this.#overlay = document.createElement('div')
    this.#overlay.style.cssText = `
      position: fixed;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: rgba(0, 0, 0, 0.85);
      display: flex;
      flex-direction: column;
      justify-content: center;
      align-items: center;
      z-index: 1000;
      font-family: system-ui, -apple-system, sans-serif;
    `

    // Create death message
    const title = document.createElement('h1')
    title.textContent = 'You Died!'
    title.style.cssText = `
      color: #ff4444;
      font-size: 4rem;
      margin: 0 0 2rem 0;
      text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.5);
    `

    // Create subtitle
    const subtitle = document.createElement('p')
    subtitle.textContent = 'Your health has been depleted'
    subtitle.style.cssText = `
      color: #cccccc;
      font-size: 1.5rem;
      margin: 0 0 3rem 0;
    `

    // Create respawn button
    const button = document.createElement('button')
    button.textContent = 'Respawn'
    button.style.cssText = `
      padding: 1rem 3rem;
      font-size: 1.5rem;
      background: #4CAF50;
      color: white;
      border: none;
      border-radius: 8px;
      cursor: pointer;
      transition: background 0.2s;
    `
    button.onmouseover = () => { button.style.background = '#45a049' }
    button.onmouseout = () => { button.style.background = '#4CAF50' }
    button.onclick = () => this.#handleRespawn()

    // Assemble overlay
    this.#overlay.appendChild(title)
    this.#overlay.appendChild(subtitle)
    this.#overlay.appendChild(button)

    // Add to document
    document.body.appendChild(this.#overlay)
  }

  /**
   * Handle respawn button click.
   * @private
   */
  #handleRespawn() {
    console.info('[DeathScreen] Respawn requested')
    if (this.#onRespawnCallback) {
      this.#onRespawnCallback()
    }
  }
}
