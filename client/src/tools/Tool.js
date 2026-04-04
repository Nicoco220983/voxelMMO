// @ts-check

/**
 * @typedef {import('../ui/VoxelHighlight.js').VoxelHighlight} VoxelHighlight
 */

/**
 * Base class for all tools.
 * Tools define their name and icon, but don't know their slot position.
 * Tools return serialized input data; the caller (Controller) decides when to send.
 * @abstract
 */
export class Tool {
  /** @type {string} Display name of the tool */
  name
  
  /** @type {string} Icon/emoji for the tool */
  icon

  /**
   * @param {string} name - Display name of the tool
   * @param {string} icon - Icon/emoji for the tool
   */
  constructor(name, icon) {
    this.name = name
    this.icon = icon
  }

  /**
   * Called when the tool is selected (player switches to this tool).
   * @param {VoxelHighlight} highlightSystem
   */
  onSelect(highlightSystem) {
    // Override in subclass if needed
  }

  /**
   * Called when the tool is deselected (player switches to another tool).
   * @param {VoxelHighlight} highlightSystem
   */
  onDeselect(highlightSystem) {
    // Override in subclass if needed
  }

  /**
   * Called when the player clicks (left mouse button) while this tool is active.
   * Returns serialized input data, or null if no action should be taken.
   * @abstract
   * @param {VoxelHighlight} highlightSystem
   * @returns {ArrayBuffer|null}
   */
  onClick(highlightSystem) {
    throw new Error('Tool.onClick() must be implemented by subclass')
  }

  /**
   * Get the highlight mode for this tool.
   * @returns {'destroy'|'create'|'select'|'none'} What kind of highlighting to show
   */
  getHighlightMode() {
    return 'none'
  }

  /**
   * Get the highlight color for this tool.
   * @returns {number} Hex color value (0xRRGGBB)
   */
  getHighlightColor() {
    return 0xFFFFFF  // Default white
  }

  /**
   * Returns true if this tool supports builder mode.
   * @returns {boolean}
   */
  supportsBuilderMode() {
    return false
  }

  /**
   * Returns true if this tool supports bulk mode.
   * @returns {boolean}
   */
  supportsBulkMode() {
    return false
  }

  /**
   * Returns true if selecting this tool should switch the hotbar into voxel mode.
   * @returns {boolean}
   */
  needsVoxelMode() {
    return false
  }

  /**
   * Returns true if selecting this tool should switch the hotbar into select mode.
   * @returns {boolean}
   */
  needsSelectMode() {
    return false
  }

  /**
   * Serialize input for builder mode activation.
   * Default implementation delegates to onClick with a mock highlight system.
   * Override for tools that need special builder mode handling.
   * @param {{x: number, y: number, z: number}} targetVoxel
   * @returns {ArrayBuffer|Array<ArrayBuffer>|null}
   */
  serializeBuilderInput(targetVoxel) {
    // Create a mock highlight system that returns our target
    const mockHighlight = {
      getHighlightedVoxel: () => targetVoxel,
      getPlacementVoxel: () => targetVoxel,
    }
    return this.onClick(mockHighlight)
  }

  /**
   * Handle bulk action completion (when user selects start and end positions).
   * Override for tools that need custom bulk behavior.
   * Default implementation uses serializeBulkInput.
   * @param {{x: number, y: number, z: number}} start
   * @param {{x: number, y: number, z: number}} end
   * @returns {ArrayBuffer|Array<ArrayBuffer>|null} Input(s) to send to server
   */
  onBulkComplete(start, end) {
    return this.serializeBulkInput?.(start.x, start.y, start.z, end.x, end.y, end.z) ?? null
  }
}

/**
 * Default tool that does nothing (for unassigned hotbar slots).
 */
export class EmptyTool extends Tool {
  constructor() {
    super('Empty', '')
  }

  /**
   * @param {VoxelHighlight} highlightSystem
   * @returns {null}
   */
  onClick(highlightSystem) {
    return null
  }
}
