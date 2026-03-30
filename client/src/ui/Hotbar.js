// @ts-check

/** @typedef {import('../tools/Tool.js').Tool} Tool */

/**
 * Hotbar UI component - bottom row of 10 selectable slots.
 * Keyboard: 1-0 to select slots (1=slot0, 0=slot9)
 * Tools are assigned to slots dynamically via setSlot().
 */
export class Hotbar {
  /** @type {HTMLElement} */
  container
  
  /** @type {Array<Tool|null>} */
  slots
  
  /** @type {number} Currently selected slot index (0-9) */
  selectedIndex
  
  /** @type {HTMLElement[]} */
  slotElements

  constructor() {
    this.selectedIndex = 0
    this.slotElements = []
    // Initialize with 10 empty slots
    this.slots = Array(10).fill(null)

    this.container = document.createElement('div')
    this.container.id = 'hotbar'
    this.render()
    document.body.appendChild(this.container)
  }

  render() {
    this.container.innerHTML = ''
    this.slotElements = []

    this.slots.forEach((slot, index) => {
      const slotEl = document.createElement('div')
      slotEl.className = 'hotbar-slot'
      if (index === this.selectedIndex) {
        slotEl.classList.add('selected')
      }

      const numberEl = document.createElement('span')
      numberEl.className = 'hotbar-number'
      numberEl.textContent = index === 9 ? '0' : String(index + 1)

      const iconEl = document.createElement('span')
      iconEl.className = 'hotbar-icon'
      iconEl.textContent = slot ? slot.icon : ''

      slotEl.appendChild(numberEl)
      slotEl.appendChild(iconEl)
      this.container.appendChild(slotEl)
      this.slotElements.push(slotEl)

      // Touch selection
      slotEl.addEventListener('pointerdown', (e) => {
        e.preventDefault()
        this.selectSlot(index)
      })
    })
  }

  /**
   * Set a tool at a specific slot index.
   * Re-renders the hotbar to show the tool's icon.
   * @param {number} index - Slot index (0-9)
   * @param {Tool} tool - The tool to assign (or null to clear)
   */
  setSlot(index, tool) {
    if (index < 0 || index > 9) return
    this.slots[index] = tool
    
    // Update the DOM element if it exists
    if (this.slotElements[index]) {
      const iconEl = this.slotElements[index].querySelector('.hotbar-icon')
      if (iconEl) {
        iconEl.textContent = tool ? tool.icon : ''
      }
    }
  }

  /**
   * Get the tool at a specific slot index.
   * @param {number} index - Slot index (0-9)
   * @returns {Tool|null}
   */
  getSlot(index) {
    if (index < 0 || index > 9) return null
    return this.slots[index]
  }

  /**
   * Select a slot by index (0-9)
   * @param {number} index
   */
  selectSlot(index) {
    if (index < 0 || index > 9) return

    // Remove selected class from old slot
    if (this.slotElements[this.selectedIndex]) {
      this.slotElements[this.selectedIndex].classList.remove('selected')
    }

    this.selectedIndex = index

    // Add selected class to new slot
    if (this.slotElements[this.selectedIndex]) {
      this.slotElements[this.selectedIndex].classList.add('selected')
    }
  }

  /**
   * Handle keyboard input for slot selection
   * @param {KeyboardEvent} e
   * @returns {boolean} true if the key was handled
   */
  handleKeyDown(e) {
    // Number keys 1-9 map to slots 0-8
    if (e.code >= 'Digit1' && e.code <= 'Digit9') {
      const slotIndex = parseInt(e.code.slice(5)) - 1
      this.selectSlot(slotIndex)
      return true
    }
    // Number key 0 maps to slot 9
    if (e.code === 'Digit0') {
      this.selectSlot(9)
      return true
    }
    // Numpad keys
    if (e.code >= 'Numpad1' && e.code <= 'Numpad9') {
      const slotIndex = parseInt(e.code.slice(6)) - 1
      this.selectSlot(slotIndex)
      return true
    }
    if (e.code === 'Numpad0') {
      this.selectSlot(9)
      return true
    }
    return false
  }

  /**
   * Get the currently selected slot info
   * @returns {{index: number, tool: Tool|null}}
   */
  getSelectedSlot() {
    return {
      index: this.selectedIndex,
      tool: this.slots[this.selectedIndex],
    }
  }
}
