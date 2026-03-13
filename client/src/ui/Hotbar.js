// @ts-check

/**
 * Hotbar UI component - bottom row of 10 selectable slots.
 * Keyboard: 1-0 to select slots (1=slot0, 0=slot9)
 */
export class Hotbar {
  /** @type {HTMLElement} */
  container
  /** @type {Array<{icon: string, name: string}>} */
  slots
  /** @type {number} Currently selected slot index (0-9) */
  selectedIndex
  /** @type {HTMLElement[]} */
  slotElements

  constructor() {
    this.selectedIndex = 0
    this.slotElements = []
    this.slots = [
      { icon: '✋', name: 'Bare Hand' },      // 1
      { icon: '➕', name: 'Create Voxel' },   // 2
      { icon: '✕', name: 'Destroy Voxel' },  // 3
      { icon: '', name: 'Empty' },           // 4
      { icon: '', name: 'Empty' },           // 5
      { icon: '', name: 'Empty' },           // 6
      { icon: '', name: 'Empty' },           // 7
      { icon: '', name: 'Empty' },           // 8
      { icon: '', name: 'Empty' },           // 9
      { icon: '', name: 'Empty' },           // 0
    ]

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
      iconEl.textContent = slot.icon

      slotEl.appendChild(numberEl)
      slotEl.appendChild(iconEl)
      this.container.appendChild(slotEl)
      this.slotElements.push(slotEl)
    })
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
   * @returns {{index: number, icon: string, name: string}}
   */
  getSelectedSlot() {
    return {
      index: this.selectedIndex,
      icon: this.slots[this.selectedIndex].icon,
      name: this.slots[this.selectedIndex].name,
    }
  }
}
