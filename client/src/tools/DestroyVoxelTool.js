// @ts-check
import { Tool } from './Tool.js'
import { InputType } from '../NetworkProtocol.js'

/**
 * @typedef {import('../GameClient.js').GameClient} GameClient
 * @typedef {import('../systems/VoxelHighlightSystem.js').VoxelHighlightSystem} VoxelHighlightSystem
 */

/**
 * Tool for destroying voxels.
 */
export class DestroyVoxelTool extends Tool {
  constructor() {
    super('Destroy Voxel', '✕')
  }

  /**
   * Serialize a VOXEL_DESTROY input frame (16 bytes).
   * Wire: type(1) + size(2) + inputType(1) + vx int32LE(4) + vy int32LE(4) + vz int32LE(4).
   * @param {number} vx  World voxel X coordinate.
   * @param {number} vy  World voxel Y coordinate.
   * @param {number} vz  World voxel Z coordinate.
   * @returns {ArrayBuffer}
   */
  static serializeInput(vx, vy, vz) {
    const buf = new ArrayBuffer(16)
    const v   = new DataView(buf)
    v.setUint8(0,   0)  // ClientMessageType.INPUT
    v.setUint16(1,  16, true)  // size
    v.setUint8(3,   InputType.VOXEL_DESTROY)
    v.setInt32(4,   vx, true)
    v.setInt32(8,   vy, true)
    v.setInt32(12,  vz, true)
    return buf
  }

  /**
   * @param {GameClient} client
   * @param {VoxelHighlightSystem} highlightSystem
   */
  onClick(client, highlightSystem) {
    const targetVoxel = highlightSystem.getHighlightedVoxel()
    if (!targetVoxel) return
    
    const inputData = DestroyVoxelTool.serializeInput(targetVoxel.x, targetVoxel.y, targetVoxel.z)
    client.sendInput(inputData)
  }

  getHighlightMode() {
    return 'destroy'
  }
}
