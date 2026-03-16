// @ts-check
import { Tool } from './Tool.js'
import { InputType } from '../NetworkProtocol.js'

/**
 * @typedef {import('../GameClient.js').GameClient} GameClient
 * @typedef {import('../systems/VoxelHighlightSystem.js').VoxelHighlightSystem} VoxelHighlightSystem
 */

/**
 * Tool for creating voxels.
 */
export class CreateVoxelTool extends Tool {
  /** @type {number} */
  #voxelType

  /**
   * @param {number} voxelType - The voxel type to create (defaults to GRASS)
   */
  constructor(voxelType = 3) { // 3 = GRASS
    super('Create Voxel', '➕')
    this.#voxelType = voxelType
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
   * @param {GameClient} client
   * @param {VoxelHighlightSystem} highlightSystem
   */
  onClick(client, highlightSystem) {
    const placementVoxel = highlightSystem.getPlacementVoxel()
    if (!placementVoxel) return
    
    const inputData = CreateVoxelTool.serializeInput(
      placementVoxel.x, placementVoxel.y, placementVoxel.z, this.#voxelType
    )
    client.sendInput(inputData)
  }

  getHighlightMode() {
    return 'create'
  }
}
