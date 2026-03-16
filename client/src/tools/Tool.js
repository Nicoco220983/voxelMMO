// @ts-check

/**
 * @typedef {import('../GameClient.js').GameClient} GameClient
 * @typedef {import('../systems/VoxelHighlightSystem.js').VoxelHighlightSystem} VoxelHighlightSystem
 */

/**
 * Base class for all tools.
 * Tools define their name and icon, but don't know their slot position.
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
   * @abstract
   * @param {GameClient} client
   * @param {VoxelHighlightSystem} highlightSystem
   */
  onClick(client, highlightSystem) {
    throw new Error('Tool.onClick() must be implemented by subclass')
  }

  /**
   * Get the highlight mode for this tool.
   * @returns {'destroy'|'create'|'none'} What kind of highlighting to show
   */
  getHighlightMode() {
    return 'none'
  }
}

/**
 * Default tool that does nothing (for unassigned hotbar slots).
 */
export class EmptyTool extends Tool {
  constructor() {
    super('Empty', '')
  }

  onClick(client, highlightSystem) {
    // Do nothing
  }
}
