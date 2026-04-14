// @ts-check
import { Tool } from './Tool.js'
import { InputType, NetworkProtocol } from '../NetworkProtocol.js'
import { ToolType } from '../ToolCatalog.js'
import { StoneVoxel, DirtVoxel, BasicVoxel, PlanksVoxel, BricksVoxel, MudVoxel, SlimeVoxel, LadderVoxel, GoblinBedVoxel } from '../voxels/index.js'

/**
 * @typedef {import('../ui/VoxelHighlight.js').VoxelHighlight} VoxelHighlight
 */

/**
 * Tool for creating voxels.
 * Client-side only tool (getToolType returns null).
 */
export class CreateVoxelTool extends Tool {
  static TOOL_ID = ToolType.CREATE_VOXEL

  // Available voxel types for the expanded view
  static VOXEL_TYPES = [
    StoneVoxel,
    DirtVoxel,
    BasicVoxel,
    PlanksVoxel,
    BricksVoxel,
    MudVoxel,
    SlimeVoxel,
    LadderVoxel,
    GoblinBedVoxel,
  ]

  /** @type {number} */
  #voxelType

  /**
   * @param {number} voxelType - The voxel type to create (defaults to BASIC)
   */
  constructor(voxelType = 1) { // 1 = BASIC
    super('Create Voxel', '➕')
    this.#voxelType = voxelType
  }

  static getToolTypeStatic() {
    return ToolType.CREATE_VOXEL
  }

  /**
   * Set the voxel type to create.
   * @param {number} voxelType
   */
  setVoxelType(voxelType) {
    this.#voxelType = voxelType
  }

  /**
   * Get the current voxel type.
   * @returns {number}
   */
  getVoxelType() {
    return this.#voxelType
  }

  /**
   * Get the index of the current voxel type in VOXEL_TYPES.
   * @returns {number}
   */
  getVoxelTypeIndex() {
    return CreateVoxelTool.VOXEL_TYPES.findIndex(v => v.type === this.#voxelType)
  }

  /**
   * Get expanded view data for the hotbar.
   * Returns voxel palette to display in slots.
   * @returns {{type: 'voxels', items: Array<import('../voxels/index.js').VoxelDef>, selectedIndex: number}}
   */
  getExpandedView() {
    return {
      type: 'voxels',
      items: CreateVoxelTool.VOXEL_TYPES,
      selectedIndex: this.getVoxelTypeIndex()
    }
  }

  /**
   * Handle selection in expanded view.
   * Called by Hotbar when user selects a voxel type.
   * @param {number} index - Index in items array
   */
  onExpandedViewSelect(index) {
    const voxelDef = CreateVoxelTool.VOXEL_TYPES[index]
    if (voxelDef) {
      this.#voxelType = voxelDef.type
    }
  }

  /**
   * Serialize a VOXEL_CREATE input frame (17 bytes).
   * Wire: type(1) + size(2) + inputType(1) + vx int32LE(4) + vy int32LE(4) + vz int32LE(4) + voxelType(1).
   * @param {number} vx  World voxel X coordinate.
   * @param {number} vy  World voxel Y coordinate.
   * @param {number} vz  World voxel Z coordinate.
   * @param {number} voxelType  Voxel type to create.
   * @returns {ArrayBuffer}
   */
  static serializeInput(vx, vy, vz, voxelType) {
    const buf = new ArrayBuffer(17)
    const v   = new DataView(buf)
    v.setUint8(0,   0)  // ClientMessageType.INPUT
    v.setUint16(1,  17, true)  // size
    v.setUint8(3,   InputType.VOXEL_CREATE)
    v.setInt32(4,   vx, true)
    v.setInt32(8,   vy, true)
    v.setInt32(12,  vz, true)
    v.setUint8(16,  voxelType)
    return buf
  }

  /**
   * @param {VoxelHighlight} highlightSystem
   * @returns {ArrayBuffer|null}
   */
  onClick(highlightSystem) {
    const target = highlightSystem.getCurrentTarget()
    if (!target) return null
    
    return CreateVoxelTool.serializeInput(
      target.x, target.y, target.z, this.#voxelType
    )
  }

  getHighlightMode() {
    return 'create'
  }

  getHighlightColor() {
    return 0x00FF00  // Green for create
  }

  needsVoxelMode() {
    return true
  }

  supportsBuilderMode() {
    return true
  }

  supportsBulkMode() {
    return true
  }

  /**
   * Serialize a BULK_VOXEL_CREATE input frame.
   * @param {number} startX
   * @param {number} startY
   * @param {number} startZ
   * @param {number} endX
   * @param {number} endY
   * @param {number} endZ
   * @returns {ArrayBuffer}
   */
  serializeBulkInput(startX, startY, startZ, endX, endY, endZ) {
    return NetworkProtocol.serializeInputBulkVoxelCreate(startX, startY, startZ, endX, endY, endZ, this.#voxelType)
  }
}
