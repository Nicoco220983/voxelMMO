// @ts-check
import { Tool } from './Tool.js'
import { InputType, NetworkProtocol } from '../NetworkProtocol.js'
import { chunkIdFromVoxelPos, getChunkPos, CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z } from '../types.js'

/**
 * @typedef {import('../ui/VoxelHighlight.js').VoxelHighlight} VoxelHighlight
 * @typedef {import('../ChunkRegistry.js').ChunkRegistry} ChunkRegistry
 */

/**
 * Tool for selecting voxels with destroy/copy/paste modes.
 * Supports bulk selection for copying regions of voxels.
 */
export class SelectVoxelTool extends Tool {
  /** @type {'destroy'|'copy'|'paste'} */
  #mode = 'destroy'

  /**
   * Copy buffer stores batched voxels from a copy operation.
   * Voxels are stored with coordinates relative to the minimum corner (anchor).
   * @type {{width: number, height: number, depth: number, copyAnchor: {x: number, y: number, z: number}, voxels: Array<{rx: number, ry: number, rz: number, type: number}>}|null}
   */
  #copyBuffer = null

  /**
   * Rotation state for paste mode (0-3, representing 0°, 90°, 180°, 270°).
   * @type {{x: number, y: number}}
   */
  #rotation = { x: 0, y: 0 }

  /** @type {ChunkRegistry|null} */
  #chunkRegistry = null

  constructor() {
    super('Select Voxel', '↖')
  }

  /**
   * Set the current sub-mode (destroy/copy/paste).
   * @param {'destroy'|'copy'|'paste'} mode
   */
  setMode(mode) {
    this.#mode = mode
  }

  /**
   * Check if currently in paste mode.
   * @returns {boolean}
   */
  isPasteMode() {
    return this.#mode === 'paste'
  }

  /**
   * Get current rotation state.
   * @returns {{x: number, y: number}} Rotation in 90° steps (0-3)
   */
  getRotation() {
    return { ...this.#rotation }
  }

  /**
   * Increment X rotation by 90°.
   * @returns {{x: number, y: number}} New rotation state
   */
  rotateX() {
    this.#rotation.x = (this.#rotation.x + 1) % 4
    return { ...this.#rotation }
  }

  /**
   * Increment Y rotation by 90°.
   * @returns {{x: number, y: number}} New rotation state
   */
  rotateY() {
    this.#rotation.y = (this.#rotation.y + 1) % 4
    return { ...this.#rotation }
  }

  /**
   * Reset rotation to zero.
   */
  resetRotation() {
    this.#rotation = { x: 0, y: 0 }
  }

  /**
   * Apply rotation to a voxel position.
   * Rotations are applied: Y first (yaw), then X (pitch).
   * @private
   * @param {{rx: number, ry: number, rz: number}} voxel - Relative voxel position
   * @returns {{rx: number, ry: number, rz: number, type: number}} Transformed position
   */
  #applyRotation(voxel) {
    if (!this.#copyBuffer) return { ...voxel }

    const { width, height, depth } = this.#copyBuffer
    
    // Center at origin for rotation
    let x = voxel.rx - (width - 1) / 2
    let y = voxel.ry - (height - 1) / 2
    let z = voxel.rz - (depth - 1) / 2

    // Apply Y rotation (yaw) - rotates around Y axis (XZ plane)
    for (let i = 0; i < this.#rotation.y; i++) {
      const newX = z
      const newZ = -x
      x = newX
      z = newZ
    }

    // Apply X rotation (pitch) - rotates around X axis (YZ plane)
    for (let i = 0; i < this.#rotation.x; i++) {
      const newY = -z
      const newZ = y
      y = newY
      z = newZ
    }

    // Calculate new dimensions after rotation
    let newWidth = width
    let newHeight = height
    let newDepth = depth

    // Y rotation swaps width/depth
    if (this.#rotation.y % 2 === 1) {
      newWidth = depth
      newDepth = width
    }

    // X rotation swaps height/depth
    if (this.#rotation.x % 2 === 1) {
      const tempHeight = newHeight
      newHeight = newDepth
      newDepth = tempHeight
    }

    // Uncenter with new dimensions
    return {
      rx: Math.round(x + (newWidth - 1) / 2),
      ry: Math.round(y + (newHeight - 1) / 2),
      rz: Math.round(z + (newDepth - 1) / 2),
      type: voxel.type
    }
  }

  /**
   * Get the current sub-mode.
   * @returns {'destroy'|'copy'|'paste'}
   */
  getMode() {
    return this.#mode
  }

  /**
   * Set the chunk registry for voxel queries (needed for copy operations).
   * @param {ChunkRegistry} chunkRegistry
   */
  setChunkRegistry(chunkRegistry) {
    this.#chunkRegistry = chunkRegistry
  }

  /**
   * Check if there's data in the copy buffer (for enabling paste mode).
   * @returns {boolean}
   */
  hasCopyBuffer() {
    return this.#copyBuffer !== null && this.#copyBuffer.voxels.length > 0
  }

  /**
   * Serialize a VOXEL_DESTROY input frame (16 bytes).
   * Wire: type(1) + size(2) + inputType(1) + vx int32LE(4) + vy int32LE(4) + vz int32LE(4).
   * @param {number} vx  World voxel X coordinate.
   * @param {number} vy  World voxel Y coordinate.
   * @param {number} vz  World voxel Z coordinate.
   * @returns {ArrayBuffer}
   */
  static serializeDestroyInput(vx, vy, vz) {
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
   * Serialize a VOXEL_CREATE input frame (17 bytes).
   * Wire: type(1) + size(2) + inputType(1) + vx int32LE(4) + vy int32LE(4) + vz int32LE(4) + voxelType(1).
   * @param {number} vx  World voxel X coordinate.
   * @param {number} vy  World voxel Y coordinate.
   * @param {number} vz  World voxel Z coordinate.
   * @param {number} voxelType  Voxel type to create.
   * @returns {ArrayBuffer}
   */
  static serializeCreateInput(vx, vy, vz, voxelType) {
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
   * Perform a single-voxel action based on current mode.
   * Mono-voxel is treated as a 1x1x1 batch - no distinction between single and bulk.
   * @param {VoxelHighlight} highlightSystem
   * @returns {ArrayBuffer|Array<ArrayBuffer>|null}
   */
  onClick(highlightSystem) {
    const target = highlightSystem.getCurrentTarget()
    if (!target) return null

    // Treat single click as a 1x1x1 batch at the target position
    return this.onBulkComplete(target, target)
  }

  /**
   * Copy a region of voxels into the copy buffer.
   * The anchor is always the minimum corner (minX, minY, minZ) of the region.
   * Resets rotation when copying new region.
   * @param {{x: number, y: number, z: number}} start
   * @param {{x: number, y: number, z: number}} end
   * @returns {boolean} true if copy succeeded
   */
  copyRegion(start, end) {
    if (!this.#chunkRegistry) return false
    
    // Reset rotation on new copy
    this.resetRotation()

    // Compute AABB bounds
    const minX = Math.min(start.x, end.x)
    const maxX = Math.max(start.x, end.x)
    const minY = Math.min(start.y, end.y)
    const maxY = Math.max(start.y, end.y)
    const minZ = Math.min(start.z, end.z)
    const maxZ = Math.max(start.z, end.z)

    const width = maxX - minX + 1
    const height = maxY - minY + 1
    const depth = maxZ - minZ + 1

    /** @type {Array<{rx: number, ry: number, rz: number, type: number}>} */
    const voxels = []

    // Query all voxels in the region
    for (let y = minY; y <= maxY; y++) {
      for (let x = minX; x <= maxX; x++) {
        for (let z = minZ; z <= maxZ; z++) {
          const type = this.#getVoxelAt(x, y, z)
          if (type !== null) {  // Include air (type 0) for paste-to-destroy behavior
            voxels.push({
              rx: x - minX,
              ry: y - minY,
              rz: z - minZ,
              type
            })
          }
        }
      }
    }

    this.#copyBuffer = {
      width,
      height,
      depth,
      copyAnchor: { x: minX, y: minY, z: minZ },
      voxels
    }

    return true
  }

  /**
   * Get all paste operations for the copy buffer at a target position.
   * Returns an array of input buffers to send.
   * Applies current rotation to voxel positions.
   * @param {{x: number, y: number, z: number}} target
   * @returns {Array<ArrayBuffer>}
   */
  getPasteInputs(target) {
    if (!this.#copyBuffer) return []

    return this.#copyBuffer.voxels.map(voxel => {
      const rotated = this.#applyRotation(voxel)
      return SelectVoxelTool.serializeCreateInput(
        target.x + rotated.rx,
        target.y + rotated.ry,
        target.z + rotated.rz,
        voxel.type
      )
    })
  }

  /**
   * Get preview voxel positions for paste mode.
   * Returns world positions of all voxels that will be pasted at the target.
   * Applies current rotation to voxel positions.
   * @param {{x: number, y: number, z: number}} target
   * @returns {Array<{x: number, y: number, z: number}>}
   */
  getPastePreviewPositions(target) {
    if (!this.#copyBuffer || this.#mode !== 'paste') return []

    return this.#copyBuffer.voxels.map(voxel => {
      const rotated = this.#applyRotation(voxel)
      return {
        x: target.x + rotated.rx,
        y: target.y + rotated.ry,
        z: target.z + rotated.rz
      }
    })
  }

  /**
   * Update paste preview visualization.
   * Called every frame when this tool is active.
   * @param {VoxelHighlight} highlightSystem
   * @param {boolean} isBuilderMode
   * @param {{x: number, y: number, z: number}|null} builderTarget
   * @param {{x: number, y: number, z: number}|null} currentTarget
   * @param {number} toolColor
   */
  update(highlightSystem, isBuilderMode, builderTarget, currentTarget, toolColor) {
    if (this.#mode !== 'paste') {
      highlightSystem.setPreviewVoxels([], 0)
      return
    }

    const pasteTarget = isBuilderMode ? builderTarget : currentTarget
    if (pasteTarget) {
      const previewPositions = this.getPastePreviewPositions(pasteTarget)
      highlightSystem.setPreviewVoxels(previewPositions, toolColor)
    } else {
      highlightSystem.setPreviewVoxels([], 0)
    }
  }

  /**
   * Get voxel type at world coordinates.
   * @private
   * @param {number} vx
   * @param {number} vy
   * @param {number} vz
   * @returns {number|null} Voxel type, or null if chunk not loaded
   */
  #getVoxelAt(vx, vy, vz) {
    if (!this.#chunkRegistry) return null
    
    const chunkId = chunkIdFromVoxelPos(vx, vy, vz)
    const chunk = this.#chunkRegistry.get(chunkId)
    if (!chunk) return null

    const { cx, cy, cz } = getChunkPos(chunkId)
    const localX = vx - cx * CHUNK_SIZE_X
    const localY = vy - cy * CHUNK_SIZE_Y
    const localZ = vz - cz * CHUNK_SIZE_Z

    return chunk.getVoxel(localX, localY, localZ)
  }

  getHighlightMode() {
    return 'select'
  }

  getHighlightColor() {
    // Red for destroy, light grey for copy, green for paste
    if (this.#mode === 'destroy') return 0xFF0000
    if (this.#mode === 'paste') return 0x00FF00  // Green for paste
    return 0xD3D3D3  // Light grey for copy
  }

  supportsBuilderMode() {
    return true
  }

  supportsBulkMode() {
    // Paste mode has no bulk mode (batch is handled by single click)
    return this.#mode !== 'paste'
  }

  needsSelectMode() {
    return true
  }

  /**
   * Serialize input for builder mode activation.
   * @param {{x: number, y: number, z: number}} targetVoxel
   * @returns {ArrayBuffer|Array<ArrayBuffer>|null}
   */
  serializeBuilderInput(targetVoxel) {
    if (this.#mode === 'destroy') {
      return SelectVoxelTool.serializeDestroyInput(targetVoxel.x, targetVoxel.y, targetVoxel.z)
    }
    if (this.#mode === 'paste' && this.#copyBuffer) {
      // Paste entire buffer (all voxels)
      return this.getPasteInputs(targetVoxel)
    }
    return null
  }

  /**
   * Serialize a BULK_VOXEL_DESTROY input frame (for destroy mode bulk).
   * @param {number} startX
   * @param {number} startY
   * @param {number} startZ
   * @param {number} endX
   * @param {number} endY
   * @param {number} endZ
   * @returns {ArrayBuffer}
   */
  serializeBulkDestroyInput(startX, startY, startZ, endX, endY, endZ) {
    return NetworkProtocol.serializeInputBulkVoxelDestroy(startX, startY, startZ, endX, endY, endZ)
  }

  /**
   * Handle bulk action completion based on current mode.
   * For copy: fills the copy buffer (local only, returns null).
   * For destroy: returns bulk destroy input.
   * For paste: returns array of voxel create inputs.
   * @param {{x: number, y: number, z: number}} start
   * @param {{x: number, y: number, z: number}} end
   * @returns {ArrayBuffer|Array<ArrayBuffer>|null}
   */
  onBulkComplete(start, end) {
    if (this.#mode === 'copy') {
      // Copy is local, no network input needed
      this.copyRegion(start, end)
      return null
    }
    
    if (this.#mode === 'destroy') {
      return this.serializeBulkDestroyInput(start.x, start.y, start.z, end.x, end.y, end.z)
    }
    
    if (this.#mode === 'paste') {
      // Use minimum corner as anchor for paste
      const anchorX = Math.min(start.x, end.x)
      const anchorY = Math.min(start.y, end.y)
      const anchorZ = Math.min(start.z, end.z)
      return this.getPasteInputs({ x: anchorX, y: anchorY, z: anchorZ })
    }
  }

  /**
   * Serialize rotation input for network (optional - for server validation).
   * Not currently used since rotation is client-side only for preview.
   * @returns {{x: number, y: number}}
   */
  serializeRotation() {
    return { ...this.#rotation }
    
    return null
  }
}
