// @ts-check

import { DestroyVoxelTool } from '../tools/DestroyVoxelTool.js'
import { CreateVoxelTool } from '../tools/CreateVoxelTool.js'
import { VoxelType } from '../VoxelTypes.js'
import { StoneVoxel, DirtVoxel, BasicVoxel, PlanksVoxel, BricksVoxel, MudVoxel, SlimeVoxel, LadderVoxel } from '../voxels/index.js'

/** @typedef {import('../tools/Tool.js').Tool} Tool */
/** @typedef {import('../voxels/index.js').VoxelDef} VoxelDef */

/**
 * Hotbar UI component - bottom row of selectable slots.
 * Supports two modes:
 * - 'tools': normal tool selection (keyboard 1-0, touch slots)
 * - 'voxels': voxel type selection when CreateVoxelTool is active
 * 
 * ESC/BACK behavior:
 * - If in voxel mode: exit voxel mode AND unselect tool
 * - Else if tool selected: unselect tool
 * - Else: let event propagate
 */
export class Hotbar {
  /** @type {HTMLElement} */
  container
  
  /** @type {HTMLElement|null} */
  #backButton = null
  
  /** @type {Array<Tool|null>} */
  slots
  
  /** @type {number} Currently selected slot index (0-9), or -1 if none */
  selectedIndex
  
  /** @type {HTMLElement[]} */
  slotElements

  // Voxel mode state
  /** @type {'tools'|'voxels'} */
  #mode = 'tools'
  /** @type {Array<Tool|null>} Saved slots when entering voxel mode */
  #savedSlots = []
  /** @type {import('../tools/CreateVoxelTool.js').CreateVoxelTool|null} */
  #createVoxelTool = null
  /** @type {VoxelDef[]} */
  #voxelItems = []

  // Callbacks
  /** @type {Function|null} Called when ESC/BACK exits voxel mode or unselects tool */
  onToolUnselected = null

  /**
   * Voxel types available in the CreateVoxelTool hotbar.
   * Player can customize this list.
   * @type {VoxelDef[]}
   */
  voxelItems

  constructor() {
    this.selectedIndex = -1  // -1 means no selection
    this.slotElements = []
    // Initialize with 10 empty slots
    this.slots = Array(10).fill(null)

    // Initialize voxel items for CreateVoxelTool (can be customized by player)
    this.voxelItems = [
      StoneVoxel,
      DirtVoxel,
      BasicVoxel,
      PlanksVoxel,
      BricksVoxel,
      MudVoxel,
      SlimeVoxel,
      LadderVoxel,
    ]

    this.container = document.createElement('div')
    this.container.id = 'hotbar'
    this.#createBackButton()
    this.render()
    document.body.appendChild(this.container)

    // Set up default tools
    this.setSlot(1, new DestroyVoxelTool())   // Key "2"
    this.setSlot(2, new CreateVoxelTool())  // Key "3"
  }

  /**
   * Create the BACK button for touch mode (hidden by default).
   * @private
   */
  #createBackButton() {
    this.#backButton = document.createElement('div')
    this.#backButton.className = 'hotbar-back-btn'
    this.#backButton.innerHTML = '<span class="hotbar-key">q</span><span class="hotbar-icon">←</span>'
    this.#backButton.style.display = 'none'
    this.#backButton.addEventListener('pointerdown', (e) => {
      e.preventDefault()
      this.handleEsc()
    })
    this.container.appendChild(this.#backButton)
  }

  render() {
    // Clear slot elements (keep back button)
    const slotsToRemove = this.container.querySelectorAll('.hotbar-slot')
    slotsToRemove.forEach(el => el.remove())
    this.slotElements = []

    this.slots.forEach((slot, index) => {
      const slotEl = document.createElement('div')
      slotEl.className = 'hotbar-slot'
      if (index === this.selectedIndex) {
        slotEl.classList.add('selected')
      }

      const numberEl = document.createElement('span')
      numberEl.className = 'hotbar-key'
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
   * In voxel mode, this updates the CreateVoxelTool's voxel type.
   * @param {number} index
   */
  selectSlot(index) {
    if (index < 0 || index > 9) return

    // In voxel mode, selecting a voxel item updates the tool but stays in voxel mode
    if (this.#mode === 'voxels') {
      const voxelItem = this.slots[index]
      if (voxelItem && this.#createVoxelTool) {
        this.#createVoxelTool.setVoxelType(voxelItem.type)
        
        // Update visual selection
        if (this.slotElements[this.selectedIndex]) {
          this.slotElements[this.selectedIndex].classList.remove('selected')
        }
        this.selectedIndex = index
        if (this.slotElements[this.selectedIndex]) {
          this.slotElements[this.selectedIndex].classList.add('selected')
        }
      }
      return
    }

    // Normal tool mode
    if (this.slotElements[this.selectedIndex]) {
      const oldTool = this.slots[this.selectedIndex]
      if (oldTool) {
        oldTool.onDeselect?.()
        if (oldTool.needsVoxelMode()) {
          this.exitVoxelMode()
        }
      }
      this.slotElements[this.selectedIndex].classList.remove('selected')
    }

    this.selectedIndex = index

    if (this.slotElements[this.selectedIndex]) {
      const newTool = this.slots[this.selectedIndex]
      if (newTool) {
        newTool.onSelect?.()
        if (newTool.needsVoxelMode()) {
          this.enterVoxelMode(newTool)
        }
      }
      this.slotElements[this.selectedIndex].classList.add('selected')
    }
  }

  /**
   * Clear the current selection (no tool selected).
   */
  clearSelection() {
    if (this.selectedIndex >= 0 && this.slotElements[this.selectedIndex]) {
      const tool = this.slots[this.selectedIndex]
      if (tool) {
        tool.onDeselect?.()
        if (tool.needsVoxelMode()) {
          this.exitVoxelMode()
        }
      }
      this.slotElements[this.selectedIndex].classList.remove('selected')
    }
    this.selectedIndex = -1
    if (this.onToolUnselected) {
      this.onToolUnselected()
    }
  }

  /**
   * Check if any slot/tool is currently selected.
   * @returns {boolean}
   */
  hasSelection() {
    return this.selectedIndex >= 0
  }

  /**
   * Check if currently in voxel mode.
   * @returns {boolean}
   */
  isInVoxelMode() {
    return this.#mode === 'voxels'
  }

  /**
   * Enter voxel selection mode.
   * Saves current slots and fills hotbar with this.voxelItems.
   * @param {import('../tools/CreateVoxelTool.js').CreateVoxelTool} createVoxelTool - The tool to configure
   */
  enterVoxelMode(createVoxelTool) {
    if (this.#mode === 'voxels') return
    
    this.#mode = 'voxels'
    this.#savedSlots = [...this.slots]
    this.#createVoxelTool = createVoxelTool
    
    // Fill slots with voxel items
    this.slots = Array(10).fill(null)
    this.voxelItems.forEach((item, i) => {
      if (i < 10) this.slots[i] = item
    })
    
    // Show back button
    if (this.#backButton) {
      this.#backButton.style.display = 'flex'
    }
    
    // Preserve selection if the voxel type matches, otherwise clear
    const currentVoxelType = createVoxelTool.getVoxelType()
    const matchingIndex = this.voxelItems.findIndex(item => item.type === currentVoxelType)
    this.selectedIndex = matchingIndex >= 0 ? matchingIndex : -1
    
    this.renderVoxelMode()
  }

  /**
   * Exit voxel selection mode and restore original tools.
   */
  exitVoxelMode() {
    if (this.#mode === 'tools') return
    
    this.#mode = 'tools'
    this.slots = [...this.#savedSlots]
    this.#voxelItems = []
    this.#createVoxelTool = null
    
    // Hide back button
    if (this.#backButton) {
      this.#backButton.style.display = 'none'
    }
    
    this.selectedIndex = -1
    this.render()
  }

  /**
   * Render the hotbar in voxel mode (shows voxel textures as icons).
   */
  renderVoxelMode() {
    // Clear slot elements (keep back button)
    const slotsToRemove = this.container.querySelectorAll('.hotbar-slot')
    slotsToRemove.forEach(el => el.remove())
    this.slotElements = []

    this.slots.forEach((slot, index) => {
      const slotEl = document.createElement('div')
      slotEl.className = 'hotbar-slot'
      if (index === this.selectedIndex) {
        slotEl.classList.add('selected')
      }

      const numberEl = document.createElement('span')
      numberEl.className = 'hotbar-key'
      numberEl.textContent = index === 9 ? '0' : String(index + 1)

      const iconEl = document.createElement('span')
      iconEl.className = 'hotbar-icon'
      
      // Show voxel texture as icon
      if (slot) {
        const voxelDef = slot
        const textureName = typeof voxelDef.textures === 'string' 
          ? voxelDef.textures 
          : voxelDef.textures.default ?? ''
        iconEl.style.backgroundImage = `url(/assets/voxels/${textureName}.png)`
        iconEl.style.backgroundSize = 'cover'
        iconEl.style.imageRendering = 'pixelated'
        iconEl.style.display = 'inline-block'
        iconEl.style.width = '32px'
        iconEl.style.height = '32px'
        iconEl.textContent = ''  // Clear any text content
      }

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
   * Handle Q key press to exit voxel mode and/or unselect tool.
   * @param {KeyboardEvent} [e] - Optional keyboard event to prevent default
   * @returns {boolean} true if handled
   */
  handleQ(e) {
    if (this.#mode === 'voxels') {
      this.exitVoxelMode()
      this.clearSelection()
      if (e) e.preventDefault()
      return true
    }
    
    if (this.hasSelection()) {
      this.clearSelection()
      if (e) e.preventDefault()
      return true
    }
    
    return false
  }

  /**
   * Handle keyboard input for slot selection.
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
   * Get the currently selected slot info.
   * In voxel mode, returns the CreateVoxelTool as the tool, not the voxel definition.
   * @returns {{index: number, tool: Tool|null}}
   */
  getSelectedSlot() {
    if (this.selectedIndex < 0) {
      return { index: -1, tool: null }
    }
    
    // In voxel mode, return the CreateVoxelTool, not the voxel definition
    if (this.#mode === 'voxels') {
      return {
        index: this.selectedIndex,
        tool: this.#createVoxelTool,
      }
    }
    
    return {
      index: this.selectedIndex,
      tool: this.slots[this.selectedIndex],
    }
  }
}
