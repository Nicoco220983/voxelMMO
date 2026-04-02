// @ts-check

/**
 * VoxelItem represents a placeable voxel type in the hotbar.
 * Unlike Tool, this is just a data holder - selecting it changes the
 * voxel type of the active CreateVoxelTool without changing the tool itself.
 */
export class VoxelItem {
  /** @type {number} Voxel type value (e.g., VoxelType.STONE) */
  voxelType
  
  /** @type {string} Display name */
  name
  
  /** @type {string} Icon/emoji for display */
  icon

  /**
   * @param {number} voxelType - The voxel type value
   * @param {string} name - Display name
   * @param {string} icon - Icon/emoji
   */
  constructor(voxelType, name, icon) {
    this.voxelType = voxelType
    this.name = name
    this.icon = icon
  }
}
