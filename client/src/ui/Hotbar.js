// @ts-check
import { HandTool } from '../tools/HandTool.js'
import { VoxelTool } from '../tools/VoxelTool.js'
import { ToolType, getToolClass } from '../ToolCatalog.js'

/**
 * @typedef {import('../tools/Tool.js').Tool} Tool
 * @typedef {import('../GameClient.js').GameClient} GameClient
 */

/**
 * Hotbar UI component for tool selection.
 * 
 * SRP: UI slots only. Manages DOM elements for tool selection.
 * No 3D visual management - that's handled by ToolVisualManager.
 * Reads selection from selfEntity.toolId.
 */
export class Hotbar {
  /** @type {GameClient|null} */
  #gameClient = null

  /** @type {HTMLElement[]} */
  slotElements

  /** @type {Array<Tool|null>} */
  slots

  /** @type {number} */
  #lastToolId = -1

  /** @type {Tool|null} */
  #currentTool = null

  /** @type {HTMLElement|null} */
  backBtn = null

  /**
   * @param {Object} options
   * @param {GameClient|null} [options.gameClient]
   */
  constructor({ gameClient = null } = {}) {
    this.#gameClient = gameClient
    this.slotElements = []
    this.slots = Array(10).fill(null)

    this.createDOM()
    this.#setDefaultTools()
  }

  /**
   * Set up default tools in slots.
   * @private
   */
  #setDefaultTools() {
    this.slots[0] = new HandTool() // Key "1"
    this.slots[1] = new VoxelTool()  // Key "2"
    this.render()
  }

  /**
   * Create the hotbar DOM elements.
   */
  createDOM() {
    let hotbar = document.getElementById('hotbar')
    if (!hotbar) {
      hotbar = document.createElement('div')
      hotbar.id = 'hotbar'
      document.body.appendChild(hotbar)
    }

    hotbar.innerHTML = ''
    this.slotElements = []

    this.backBtn = document.createElement('div')
    this.backBtn.className = 'hotbar-back-btn'
    this.backBtn.textContent = '←'
    this.backBtn.title = 'Back'
    this.backBtn.addEventListener('click', () => this.clearSelection())
    this.backBtn.addEventListener('touchstart', (e) => {
      e.preventDefault()
      this.clearSelection()
    }, { passive: false })
    hotbar.appendChild(this.backBtn)

    for (let i = 0; i < 10; i++) {
      const slot = document.createElement('div')
      slot.className = 'hotbar-slot'
      slot.dataset.index = String(i)

      const number = document.createElement('span')
      number.className = 'hotbar-number'
      number.textContent = String((i + 1) % 10)
      slot.appendChild(number)

      const icon = document.createElement('span')
      icon.className = 'hotbar-icon'
      slot.appendChild(icon)

      hotbar.appendChild(slot)
      this.slotElements.push(slot)
    }
  }

  /**
   * Get the currently selected slot index based on selfEntity.toolId.
   * @returns {number} Slot index (0-9) or -1 if no match
   * @private
   */
  #getSelectedSlotIndex() {
    const currentToolId = this.#gameClient?.selfEntity?.toolId
    
    // If no tool selected yet (null), default to HandTool (slot 0)
    // Server spawns players with HAND tool by default, but doesn't serialize
    // default ToolComponent values, so client initially sees null
    if (currentToolId === null || currentToolId === undefined) {
      // HandTool is always in slot 0 by default
      const handTool = this.slots[0]
      if (handTool && handTool.getToolType?.() === ToolType.HAND) {
        return 0
      }
      return -1
    }
    
    // Find slot with matching tool type
    for (let i = 0; i < this.slots.length; i++) {
      const tool = this.slots[i]
      if (!tool) continue
      
      const toolType = tool.getToolType?.()
      if (toolType === currentToolId) {
        return i
      }
    }
    
    return -1
  }

  /**
   * Get the current tool based on selfEntity.toolId.
   * For client-side tools, returns the slot's tool instance.
   * @returns {Tool|null}
   * @private
   */
  #getCurrentTool() {
    const selectedIndex = this.#getSelectedSlotIndex()
    if (selectedIndex >= 0) {
      return this.slots[selectedIndex]
    }
    return null
  }

  /**
   * Render the hotbar UI based on current state.
   * Called each frame or when state changes.
   */
  render() {
    const currentToolId = this.#gameClient?.selfEntity?.toolId ?? ToolType.NONE
    const selectedIndex = this.#getSelectedSlotIndex()
    const currentTool = this.#getCurrentTool()
    
    // Check if tool changed
    if (currentToolId !== this.#lastToolId) {
      this.#onToolChanged(currentToolId, currentTool)
    }
    
    // Get expanded view from current tool (if any)
    const expandedView = currentTool?.getExpandedView?.()
    
    // Show/hide hotbar back button based on expanded view
    if (this.backBtn) {
      const hasExpandedView = !!expandedView
      this.backBtn.style.display = hasExpandedView ? 'flex' : 'none'
    }

    // Render each slot
    for (let i = 0; i < 10; i++) {
      const slotEl = this.slotElements[i]
      const icon = slotEl.querySelector('.hotbar-icon')
      
      // Clear previous state
      icon.textContent = ''
      icon.style.backgroundImage = ''
      slotEl.title = ''
      
      if (expandedView) {
        // Expanded mode: show tool's expanded view items
        this.#renderExpandedSlot(icon, slotEl, i, expandedView, selectedIndex)
      } else {
        // Normal mode: show tool icon
        this.#renderToolSlot(icon, slotEl, i, selectedIndex)
      }
    }
  }

  /**
   * Render a slot in expanded view mode.
   * @private
   */
  #renderExpandedSlot(icon, slotEl, index, expandedView, selectedIndex) {
    const item = expandedView.items[index]
    
    if (item) {
      if (expandedView.type === 'voxels') {
        // Voxel mode: show voxel texture
        const textureName = typeof item.textures === 'string' 
          ? item.textures 
          : item.textures?.default ?? ''
        
        if (textureName) {
          icon.style.backgroundImage = `url(/assets/voxels/${textureName}.png)`
          icon.style.backgroundSize = 'cover'
          icon.style.imageRendering = 'pixelated'
        } else {
          icon.textContent = item.name?.[0]?.toUpperCase() || '?'
        }
        slotEl.title = item.name || `Voxel ${item.type}`
      } else if (expandedView.type === 'select') {
        // Select mode: show option icon
        icon.textContent = item.icon
        slotEl.title = item.name
      }
    }
    
    // Highlight selected slot
    slotEl.classList.toggle('selected', index === expandedView.selectedIndex)
  }

  /**
   * Render a slot in normal tool mode.
   * @private
   */
  #renderToolSlot(icon, slotEl, index, selectedIndex) {
    const tool = this.slots[index]
    
    if (tool) {
      icon.textContent = tool.icon || ''
      slotEl.title = tool.name || ''
    }
    
    // Highlight selected slot
    slotEl.classList.toggle('selected', index === selectedIndex)
  }

  /**
   * Handle tool change.
   * @private
   */
  #onToolChanged(toolId, tool) {
    this.#lastToolId = toolId
    this.#currentTool = tool
  }

  /**
   * Handle keydown events for hotbar (number keys 1-0, ESC).
   * Called by KeyboardController.
   * @param {KeyboardEvent} e
   * @returns {boolean} true if handled
   */
  handleKeyDown(e) {
    // Number keys 1-0 for slot selection
    const slotMap = {
      'Digit1': 0, 'Digit2': 1, 'Digit3': 2, 'Digit4': 3, 'Digit5': 4,
      'Digit6': 5, 'Digit7': 6, 'Digit8': 7, 'Digit9': 8, 'Digit0': 9,
      'Numpad1': 0, 'Numpad2': 1, 'Numpad3': 2, 'Numpad4': 3, 'Numpad5': 4,
      'Numpad6': 5, 'Numpad7': 6, 'Numpad8': 7, 'Numpad9': 8, 'Numpad0': 9,
    }

    if (slotMap[e.code] !== undefined) {
      this.selectSlot(slotMap[e.code])
      return true
    }

    // ESC clears selection
    if (e.code === 'Escape') {
      this.clearSelection()
      return true
    }

    return false
  }

  /**
   * Handle slot selection (key press).
   * 
   * If the current tool has an expanded view and the selected slot
   * is within the expanded items, handle it as an expanded view selection.
   * 
   * @param {number} index - Slot index (0-9)
   */
  selectSlot(index) {
    if (index < 0 || index > 9) return

    const currentTool = this.#getCurrentTool()
    const expandedView = currentTool?.getExpandedView?.()
    
    // Check if we're in expanded view and selecting an expanded item
    if (expandedView && index < expandedView.items.length) {
      // Handle expanded view selection
      currentTool.onExpandedViewSelect?.(index)
      this.render()
      return
    }

    // Normal tool selection
    const requestedTool = this.slots[index]
    if (!requestedTool) return
    
    const toolId = requestedTool.getToolType?.() ?? ToolType.NONE
    
    // Send to server (all tools now have server-side IDs)
    this.#gameClient?.sendToolSelect(toolId)
    
    // Update UI immediately for responsiveness
    // (Server will eventually confirm with same value)
    this.render()
  }

  /**
   * Clear the current selection (no tool selected).
   */
  clearSelection() {
    // Notify current tool it's being deselected
    const currentTool = this.#getCurrentTool()
    currentTool?.onDeselect?.()
    
    // Send NONE to server
    this.#gameClient?.sendToolSelect(ToolType.NONE)
    this.render()
  }

  /**
   * Check if a tool is currently selected.
   * @returns {boolean}
   */
  hasSelection() {
    return this.#getSelectedSlotIndex() >= 0
  }

  /**
   * Get the currently selected slot data.
   * @returns {{index: number, tool: Tool|null}}
   */
  getSelectedSlot() {
    const index = this.#getSelectedSlotIndex()
    return {
      index,
      tool: index >= 0 ? this.slots[index] : null
    }
  }

  /**
   * Set a tool in a specific slot.
   * @param {number} index - Slot index (0-9)
   * @param {Tool} tool - The tool to set
   */
  setTool(index, tool) {
    if (index < 0 || index > 9) return
    this.slots[index] = tool
    this.render()
  }

  /**
   * Remove a tool from a slot.
   * @param {number} index - Slot index (0-9)
   */
  removeTool(index) {
    if (index < 0 || index > 9) return
    this.slots[index] = null
    this.render()
  }



  /**
   * Get the current tool instance.
   * @returns {Tool|null}
   */
  getCurrentTool() {
    return this.#getCurrentTool()
  }

  /**
   * Clean up resources.
   */
  destroy() {
    // Nothing to clean up - no 3D visuals managed here
  }
}
