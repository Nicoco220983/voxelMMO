// @ts-check

/**
 * @typedef {import('../systems/VoxelHighlightSystem.js').VoxelHighlightSystem} VoxelHighlightSystem
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
   * @param {VoxelHighlightSystem} highlightSystem
   */
  onSelect(highlightSystem) {
    // Override in subclass if needed
  }

  /**
   * Called when the tool is deselected (player switches to another tool).
   * @param {VoxelHighlightSystem} highlightSystem
   */
  onDeselect(highlightSystem) {
    // Override in subclass if needed
  }

  /**
   * Called when the player clicks (left mouse button) while this tool is active.
   * Returns serialized input data, or null if no action should be taken.
   * @abstract
   * @param {VoxelHighlightSystem} highlightSystem
   * @returns {ArrayBuffer|null}
   */
  onClick(highlightSystem) {
    throw new Error('Tool.onClick() must be implemented by subclass')
  }

  /**
   * Get the highlight mode for this tool.
   * @returns {'destroy'|'create'|'none'} What kind of highlighting to show
   */
  getHighlightMode() {
    return 'none'
  }

  /**
   * Returns true if this tool supports builder mode.
   * @returns {boolean}
   */
  supportsBuilderMode() {
    return false
  }

  /**
   * Serialize input for builder mode activation.
   * Default implementation delegates to onClick with a mock highlight system.
   * Override for tools that need special builder mode handling.
   * @param {{x: number, y: number, z: number}} targetVoxel
   * @returns {ArrayBuffer|null}
   */
  serializeBuilderInput(targetVoxel) {
    // Create a mock highlight system that returns our target
    const mockHighlight = {
      getHighlightedVoxel: () => targetVoxel,
      getPlacementVoxel: () => targetVoxel,
    }
    return this.onClick(mockHighlight)
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
   * @param {VoxelHighlightSystem} highlightSystem
   * @returns {null}
   */
  onClick(highlightSystem) {
    return null
  }
}
