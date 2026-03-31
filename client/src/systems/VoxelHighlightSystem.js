// @ts-check
import * as THREE from 'three'
import { VoxelType, chunkIdFromVoxelPos, CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z, getChunkPos } from '../types.js'

/** @typedef {import('../types.js').ChunkId} ChunkId */
/** @typedef {import('../ChunkRegistry.js').ChunkRegistry} ChunkRegistry */
/** @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar */
/** @typedef {import('../controllers/BaseController.js').BaseController} BaseController */

/** Maximum reach distance for voxel highlighting (in voxels) */
const MAX_REACH_DISTANCE = 5

/** Highlight color - yellow for destroy */
const HIGHLIGHT_COLOR_DESTROY = 0xFFFF00
/** Highlight color - green for create */
const HIGHLIGHT_COLOR_CREATE = 0x00FF00
/** Highlight color - blue for builder mode */
const HIGHLIGHT_COLOR_BUILDER = 0x0088FF
/** Highlight color - orange for bulk builder selection */
const HIGHLIGHT_COLOR_BULK = 0xFF8800

/**
 * @class VoxelHighlightSystem
 * @description Manages the wireframe highlight box for voxel destruction.
 * Casts a ray from camera center and highlights the first non-air voxel within reach.
 */
export class VoxelHighlightSystem {
  /** @type {THREE.Scene} */
  #scene

  /** @type {THREE.LineSegments|null} */
  #highlightMesh = null

  /** @type {boolean} */
  #isVisible = false

  /** @type {{x: number, y: number, z: number}|null} */
  #highlightedVoxel = null
  
  /** @type {{x: number, y: number, z: number}|null} */
  #placementVoxel = null
  
  /** @type {'destroy'|'create'|'none'} */
  #currentMode = 'none'

  // Builder mode state
  /** @type {boolean} */
  #builderMode = false
  /** @type {{x: number, y: number, z: number}|null} */
  #builderTarget = null
  /** @type {number} */
  #entryYaw = 0

  // Bulk builder mode state
  /** @type {THREE.LineSegments|null} */
  #bulkHighlightMesh = null
  /** @type {{x: number, y: number, z: number}|null} */
  #bulkStart = null
  /** @type {boolean} True when waiting for end voxel selection (start is set, waiting for click #2) */
  #bulkSelectingEnd = false

  /**
   * @param {THREE.Scene} scene
   */
  constructor(scene) {
    this.#scene = scene
    this.#createHighlightMesh()
    this.#createBulkHighlightMesh()
  }

  /**
   * Create the bulk selection highlight mesh (initially hidden).
   * This shows an orange wireframe box covering the selected volume.
   * @private
   */
  #createBulkHighlightMesh() {
    // Create a unit cube that we'll scale to fit the selection
    const geometry = new THREE.BoxGeometry(1, 1, 1)
    const edges = new THREE.EdgesGeometry(geometry)
    const material = new THREE.LineBasicMaterial({ 
      color: HIGHLIGHT_COLOR_BULK,
      linewidth: 2 
    })
    
    this.#bulkHighlightMesh = new THREE.LineSegments(edges, material)
    this.#bulkHighlightMesh.visible = false
    this.#scene.add(this.#bulkHighlightMesh)
  }

  /**
   * Sync visualization state with controller and hotbar.
   * Updates builder mode visuals and bulk preview.
   * Does NOT handle tool activation - caller should check controller.toolActivated
   * and call controller.sendToolInput() separately.
   * @param {THREE.PerspectiveCamera} camera
   * @param {Hotbar} hotbar
   * @param {BaseController} controller
   * @param {ChunkRegistry} chunkRegistry
   */
  sync(camera, hotbar, controller, chunkRegistry) {
    const currentTool = hotbar.getSelectedSlot().tool
    const highlightMode = currentTool ? currentTool.getHighlightMode() : 'none'

    // Update base highlight system
    this.update(camera, chunkRegistry, highlightMode)

    // Handle builder mode entry/exit
    if (controller.builderModeChanged) {
      if (controller.builderMode) {
        this.setBuilderMode(true, controller.entryYaw)
        this.setBuilderTargetFromRaycast(camera, chunkRegistry, highlightMode)
      } else {
        this.setBuilderMode(false)
        this.setBulkPreview(null, false)
      }
    }

    // Update bulk preview in 'start' phase
    if (controller.bulkBuilderMode && controller.bulkPhase === 'start') {
      this.updateBulkPreview()
    }

    // Handle bulk preview visualization based on controller state
    if (controller.bulkBuilderMode) {
      if (controller.bulkPhase === 'none') {
        this.setBulkPreview(null, false)
      } else if (controller.bulkPhase === 'start') {
        this.setBulkPreview(controller.bulkStartVoxel, true)
      }
    }

    // Handle builder mode voxel movement
    if (controller.builderMode) {
      const delta = controller.builderMoveDelta
      if (delta.x !== 0 || delta.y !== 0 || delta.z !== 0) {
        this.moveBuilderTarget(delta.x, delta.y, delta.z)
      }
    }
  }

  /**
   * Create the wireframe highlight mesh (initially hidden).
   * @private
   */
  #createHighlightMesh() {
    // Create a slightly larger box (1.05x) to avoid z-fighting
    const geometry = new THREE.BoxGeometry(1.05, 1.05, 1.05)
    const edges = new THREE.EdgesGeometry(geometry)
    const material = new THREE.LineBasicMaterial({ 
      color: HIGHLIGHT_COLOR_DESTROY,
      linewidth: 2 
    })
    
    this.#highlightMesh = new THREE.LineSegments(edges, material)
    this.#highlightMesh.visible = false
    this.#scene.add(this.#highlightMesh)
  }

  /**
   * Update highlight color based on tool mode.
   * @private
   * @param {'destroy'|'create'|'none'} mode
   */
  #updateHighlightColor(mode) {
    if (!this.#highlightMesh) return
    const material = /** @type {THREE.LineBasicMaterial} */ (this.#highlightMesh.material)
    if (this.#builderMode) {
      material.color.setHex(HIGHLIGHT_COLOR_BUILDER)
    } else if (mode === 'create') {
      material.color.setHex(HIGHLIGHT_COLOR_CREATE)
    } else {
      material.color.setHex(HIGHLIGHT_COLOR_DESTROY)
    }
  }

  /**
   * Enable/disable builder mode and update visual state.
   * @param {boolean} enabled
   * @param {number} [entryYaw] - Player yaw at builder mode entry time
   */
  setBuilderMode(enabled, entryYaw = 0) {
    this.#builderMode = enabled
    this.#entryYaw = entryYaw
    this.#updateHighlightColor(this.#currentMode)
  }

  /**
   * Set the builder target from current raycast result.
   * Call this when entering builder mode.
   * @param {THREE.PerspectiveCamera} camera
   * @param {ChunkRegistry} chunkRegistry
   * @param {'destroy'|'create'|'none'} toolMode
   * @returns {boolean} true if a target was found and set
   */
  setBuilderTargetFromRaycast(camera, chunkRegistry, toolMode) {
    if (toolMode === 'none') {
      this.#builderTarget = null
      return false
    }

    // Get camera direction from yaw (Y) and pitch (X) rotation
    const yaw = camera.rotation.y
    const pitch = camera.rotation.x

    const cosPitch = Math.cos(pitch)
    const dirX = -Math.sin(yaw) * cosPitch
    const dirY = Math.sin(pitch)
    const dirZ = -Math.cos(yaw) * cosPitch

    const originX = camera.position.x
    const originY = camera.position.y
    const originZ = camera.position.z

    const hit = this.#castRay(originX, originY, originZ, dirX, dirY, dirZ, chunkRegistry, toolMode)

    if (hit) {
      // Use placement position for create, highlighted position for destroy
      if (toolMode === 'create') {
        this.#builderTarget = { x: hit.placementX, y: hit.placementY, z: hit.placementZ }
      } else {
        this.#builderTarget = { x: hit.x, y: hit.y, z: hit.z }
      }
      this.#updateHighlightPosition()
      return true
    }

    this.#builderTarget = null
    return false
  }

  /**
   * Move the builder target by the given delta.
   * @param {number} dx
   * @param {number} dy
   * @param {number} dz
   */
  moveBuilderTarget(dx, dy, dz) {
    if (!this.#builderTarget) return
    this.#builderTarget.x += dx
    this.#builderTarget.y += dy
    this.#builderTarget.z += dz
    this.#updateHighlightPosition()
  }

  /**
   * Get the current builder target position.
   * @returns {{x: number, y: number, z: number}|null}
   */
  getBuilderTarget() {
    return this.#builderTarget
  }

  /**
   * Update highlight mesh position to builder target.
   * @private
   */
  #updateHighlightPosition() {
    if (!this.#highlightMesh || !this.#builderTarget) return
    this.#highlightMesh.position.set(
      this.#builderTarget.x + 0.5,
      this.#builderTarget.y + 0.5,
      this.#builderTarget.z + 0.5
    )
  }

  /**
   * Set the bulk selection preview.
   * Shows a single orange wireframe box covering the entire selection volume.
   * @param {{x: number, y: number, z: number}|null} start - Start voxel, null to clear
   * @param {boolean} selectingEnd - True if waiting for end voxel selection (dynamic preview)
   */
  setBulkPreview(start, selectingEnd = false) {
    this.#bulkStart = start
    this.#bulkSelectingEnd = selectingEnd
    this.#updateBulkVisuals()
  }

  /**
   * Update the bulk highlight mesh based on current #bulkStart.
   * If #bulkSelectingEnd is true, uses #builderTarget for dynamic preview.
   * @private
   */
  #updateBulkVisuals() {
    if (!this.#bulkHighlightMesh) return
    
    if (!this.#bulkStart) {
      this.#bulkHighlightMesh.visible = false
      return
    }
    
    // If selecting end, use current builder target for dynamic preview
    const actualEnd = this.#bulkSelectingEnd ? this.#builderTarget : this.#bulkStart
    if (!actualEnd) {
      this.#bulkHighlightMesh.visible = false
      return
    }
    
    // Calculate bounds (inclusive)
    const minX = Math.min(this.#bulkStart.x, actualEnd.x)
    const maxX = Math.max(this.#bulkStart.x, actualEnd.x)
    const minY = Math.min(this.#bulkStart.y, actualEnd.y)
    const maxY = Math.max(this.#bulkStart.y, actualEnd.y)
    const minZ = Math.min(this.#bulkStart.z, actualEnd.z)
    const maxZ = Math.max(this.#bulkStart.z, actualEnd.z)
    
    // Size of the selection box (add 1.05 for visibility, same as highlight)
    const sizeX = maxX - minX + 1.05
    const sizeY = maxY - minY + 1.05
    const sizeZ = maxZ - minZ + 1.05
    
    // Center position
    const centerX = (minX + maxX) / 2 + 0.5
    const centerY = (minY + maxY) / 2 + 0.5
    const centerZ = (minZ + maxZ) / 2 + 0.5
    
    this.#bulkHighlightMesh.position.set(centerX, centerY, centerZ)
    this.#bulkHighlightMesh.scale.set(sizeX, sizeY, sizeZ)
    this.#bulkHighlightMesh.visible = true
  }

  /**
   * Update bulk preview if in 'selecting end' phase (dynamic end following builder target).
   * Call this after moving the builder target when selecting end voxel.
   */
  updateBulkPreview() {
    // Only update visuals if selecting end and we have a builder target
    if (this.#bulkStart && this.#bulkSelectingEnd && this.#builderTarget) {
      this.#updateBulkVisuals()
    }
  }

  /**
   * Update the highlight based on camera position and tool mode.
   * @param {THREE.PerspectiveCamera} camera
   * @param {ChunkRegistry} chunkRegistry
   * @param {'destroy'|'create'|'none'} highlightMode - Current tool highlight mode
   */
  update(camera, chunkRegistry, highlightMode) {
    // Only show highlight for destroy or create modes
    if (highlightMode === 'none') {
      this.#hide()
      return
    }
    
    // Update color if mode changed
    if (this.#currentMode !== highlightMode) {
      this.#currentMode = highlightMode
      this.#updateHighlightColor(highlightMode)
    }

    // In builder mode, don't raycast - just show the builder target
    if (this.#builderMode) {
      if (this.#builderTarget) {
        this.#highlightMesh.visible = true
        this.#isVisible = true
      } else {
        this.#hide()
      }
      return
    }

    // Get camera direction from yaw (Y) and pitch (X) rotation
    const yaw = camera.rotation.y
    const pitch = camera.rotation.x

    // Calculate forward vector (camera looks down negative Z in local space)
    const cosPitch = Math.cos(pitch)
    const dirX = -Math.sin(yaw) * cosPitch
    const dirY = Math.sin(pitch)
    const dirZ = -Math.cos(yaw) * cosPitch

    // Camera position in voxel coordinates
    const originX = camera.position.x
    const originY = camera.position.y
    const originZ = camera.position.z

    // Cast ray to find target voxel
    const hit = this.#castRay(originX, originY, originZ, dirX, dirY, dirZ, chunkRegistry, highlightMode)

    if (hit) {
      this.#showAt(hit.x, hit.y, hit.z, hit.placementX, hit.placementY, hit.placementZ)
    } else {
      this.#hide()
    }
  }

  /**
   * Cast a ray through the voxel grid using 3D DDA algorithm.
   * For DESTROY tool: returns the first non-air voxel.
   * For CREATE tool: returns the empty voxel adjacent to the first non-air voxel
   * (the one the ray entered from - closest to player along the ray).
   * @private
   * @param {number} originX
   * @param {number} originY
   * @param {number} originZ
   * @param {number} dirX
   * @param {number} dirY
   * @param {number} dirZ
   * @param {ChunkRegistry} chunkRegistry
   * @param {'destroy'|'create'|'none'} toolMode - Current tool mode
   * @returns {{x: number, y: number, z: number, placementX: number, placementY: number, placementZ: number}|null}
   */
  #castRay(originX, originY, originZ, dirX, dirY, dirZ, chunkRegistry, toolMode) {
    // Normalize direction
    const len = Math.sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ)
    const dx = dirX / len
    const dy = dirY / len
    const dz = dirZ / len

    // Start slightly offset from origin to avoid self-intersection
    const startOffset = 0.01
    let t = startOffset
    
    // Current position along ray
    let currX = originX + dx * t
    let currY = originY + dy * t
    let currZ = originZ + dz * t
    
    // Current voxel coordinates
    let vx = Math.floor(currX)
    let vy = Math.floor(currY)
    let vz = Math.floor(currZ)
    
    // Track previous voxel (the one we came from)
    let prevX = vx, prevY = vy, prevZ = vz

    // Maximum travel distance
    const maxT = MAX_REACH_DISTANCE
    
    // Step size (voxel-sized steps along ray)
    const stepSize = 0.5  // Check every 0.5 voxels to ensure we don't miss any

    while (t < maxT) {
      // Check current voxel
      const voxel = this.#getVoxelAt(vx, vy, vz, chunkRegistry)
      
      if (voxel !== null && voxel !== VoxelType.AIR) {
        // Found a solid voxel
        if (toolMode === 'destroy') {
          // DESTROY tool: highlight the solid voxel itself
          return { x: vx, y: vy, z: vz, placementX: vx, placementY: vy, placementZ: vz }
        } else {
          // CREATE tool: highlight the previous (empty) voxel we came from
          return { x: prevX, y: prevY, z: prevZ, placementX: prevX, placementY: prevY, placementZ: prevZ }
        }
      }
      
      // Current voxel is empty, remember it as the previous one
      prevX = vx
      prevY = vy
      prevZ = vz

      // Step along ray
      t += stepSize
      currX = originX + dx * t
      currY = originY + dy * t
      currZ = originZ + dz * t
      
      // Update voxel coordinates
      vx = Math.floor(currX)
      vy = Math.floor(currY)
      vz = Math.floor(currZ)
      
      // If we haven't changed voxels, continue stepping
      if (vx === prevX && vy === prevY && vz === prevZ) continue
    }

    return null
  }

  /**
   * Get voxel type at world coordinates.
   * @private
   * @param {number} vx
   * @param {number} vy
   * @param {number} vz
   * @param {ChunkRegistry} chunkRegistry
   * @returns {number|null} Voxel type, or null if chunk not loaded
   */
  #getVoxelAt(vx, vy, vz, chunkRegistry) {
    const chunkId = chunkIdFromVoxelPos(vx, vy, vz)
    const chunk = chunkRegistry.get(chunkId)
    if (!chunk) return null

    const { cx, cy, cz } = getChunkPos(chunkId)
    const localX = vx - cx * CHUNK_SIZE_X
    const localY = vy - cy * CHUNK_SIZE_Y
    const localZ = vz - cz * CHUNK_SIZE_Z

    return chunk.getVoxel(localX, localY, localZ)
  }

  /**
   * Show highlight at specified voxel coordinates.
   * @private
   * @param {number} vx
   * @param {number} vy
   * @param {number} vz
   * @param {number} placementX
   * @param {number} placementY
   * @param {number} placementZ
   */
  #showAt(vx, vy, vz, placementX, placementY, placementZ) {
    if (!this.#highlightMesh) return
    this.#highlightMesh.position.set(vx + 0.5, vy + 0.5, vz + 0.5)
    this.#highlightMesh.visible = true
    this.#isVisible = true
    this.#highlightedVoxel = { x: vx, y: vy, z: vz }
    this.#placementVoxel = { x: placementX, y: placementY, z: placementZ }
  }

  /**
   * Hide the highlight.
   * @private
   */
  #hide() {
    if (!this.#highlightMesh) return
    this.#highlightMesh.visible = false
    this.#isVisible = false
    this.#highlightedVoxel = null
    this.#placementVoxel = null
  }

  /**
   * Get the currently highlighted voxel coordinates.
   * @returns {{x: number, y: number, z: number}|null} Null if no voxel is highlighted.
   */
  getHighlightedVoxel() {
    return this.#highlightedVoxel
  }
  
  /**
   * Get the placement voxel coordinates (for CREATE tool, this is where the voxel will be placed).
   * @returns {{x: number, y: number, z: number}|null} Null if no placement position available.
   */
  getPlacementVoxel() {
    return this.#placementVoxel
  }

  /**
   * Dispose of highlight resources.
   */
  dispose() {
    if (this.#highlightMesh) {
      this.#scene.remove(this.#highlightMesh)
      this.#highlightMesh.geometry.dispose()
      const material = this.#highlightMesh.material
      if (Array.isArray(material)) {
        material.forEach(m => m.dispose())
      } else {
        material.dispose()
      }
      this.#highlightMesh = null
    }
    if (this.#bulkHighlightMesh) {
      this.#scene.remove(this.#bulkHighlightMesh)
      this.#bulkHighlightMesh.geometry.dispose()
      const material = this.#bulkHighlightMesh.material
      if (Array.isArray(material)) {
        material.forEach(m => m.dispose())
      } else {
        material.dispose()
      }
      this.#bulkHighlightMesh = null
    }
  }
}
