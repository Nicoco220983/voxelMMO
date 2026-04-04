// @ts-check
import * as THREE from 'three'
import { VoxelType } from '../VoxelTypes.js'
import { chunkIdFromVoxelPos, CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z, getChunkPos } from '../types.js'

/** @typedef {import('../types.js').ChunkId} ChunkId */
/** @typedef {import('../ChunkRegistry.js').ChunkRegistry} ChunkRegistry */
/** @typedef {import('../ui/Hotbar.js').Hotbar} Hotbar */

/** Maximum reach distance for voxel highlighting (in voxels) */
const MAX_REACH_DISTANCE = 8



/**
 * @class VoxelHighlight
 * @description Manages the semi-transparent highlight box for voxel selection.
 * Casts a ray from camera center and highlights the first non-air voxel within reach.
 * 
 * This class only handles visualization. Mode state and target selection logic
 * is in BaseController. Use setTarget() to update visuals.
 */
export class VoxelHighlight {
  /** @type {THREE.Scene} */
  #scene

  /** @type {THREE.Mesh|null} */
  #highlightMesh = null

  /** @type {boolean} */
  #isVisible = false

  /** @type {boolean} */
  #isBuilderMode = false

  /** @type {THREE.Group|null} */
  #builderGizmo = null

  /** Current highlighted voxel (for destroy mode)
   * @type {{x: number, y: number, z: number}|null}
   */
  #highlightedVoxel = null

  /** Current placement voxel (for create mode - empty voxel adjacent to hit)
   * @type {{x: number, y: number, z: number}|null}
   */
  #placementVoxel = null

  /** @type {number} Current highlight color */
  #currentColor = 0xFFFFFF

  /** Preview meshes for multi-voxel preview (e.g., paste mode)
   * @type {THREE.Mesh[]}
   */
  #previewMeshes = []

  /**
   * @param {THREE.Scene} scene
   */
  constructor(scene) {
    this.#scene = scene
    this.#createHighlightMesh()
    this.#createBuilderGizmo()
  }

  /**
   * Create the highlight mesh (initially hidden).
   * Shows a semi-transparent colored box.
   * @private
   */
  #createHighlightMesh() {
    // Create a slightly larger box (1.05x) to avoid z-fighting
    const geometry = new THREE.BoxGeometry(1.05, 1.05, 1.05)
    const material = new THREE.MeshBasicMaterial({
      color: this.#currentColor,
      transparent: true,
      opacity: 0.5,
      depthWrite: false
    })

    this.#highlightMesh = new THREE.Mesh(geometry, material)
    this.#highlightMesh.visible = false
    this.#scene.add(this.#highlightMesh)
  }

  /**
   * Update highlight color.
   * @private
   * @param {number} color - Hex color value (0xRRGGBB)
   */
  #updateHighlightColor(color) {
    if (!this.#highlightMesh) return
    const material = /** @type {THREE.MeshBasicMaterial} */ (this.#highlightMesh.material)
    material.color.setHex(color)
    if (this.#builderGizmo) {
      this.#builderGizmo.children.forEach((child) => {
        /** @type {THREE.MeshBasicMaterial} */ (child.material).color.setHex(color)
      })
    }
  }

  /**
   * Set the current target for visualization.
   * Called by main loop with target from BaseController.
   * @param {{x: number, y: number, z: number}|null} target
   * @param {number} color - Hex color value (0xRRGGBB), 0 to hide
   * @param {'destroy'|'create'|'select'} mode - Tool mode for tracking voxel type
   * @param {boolean} [isBuilderMode=false] - Whether builder mode is active
   * @param {'destroy'|'copy'|'paste'|null} [subMode=null] - Sub-mode for select tool
   */
  setTarget(target, color, mode, isBuilderMode = false, subMode = null) {
    // Update color if changed
    if (this.#currentColor !== color) {
      this.#currentColor = color
      this.#updateHighlightColor(color)
    }

    this.#isBuilderMode = isBuilderMode

    if (!target || color === 0) {
      this.#hide()
      return
    }

    // For create mode, target is the placement position
    // For destroy/select mode (except paste), target is the highlighted voxel
    // For paste mode, target is the placement position (like create)
    if (mode === 'create' || (mode === 'select' && subMode === 'paste')) {
      this.#placementVoxel = target
      // Highlighted voxel is adjacent, but we don't track it separately
      this.#highlightedVoxel = null
    } else {
      this.#highlightedVoxel = target
      this.#placementVoxel = null
    }

    this.#showAt(target.x, target.y, target.z)
  }

  /**
   * Perform raycast to find target voxel.
   * Used by BaseController to determine initial targets on mode entry.
   * @param {THREE.PerspectiveCamera} camera
   * @param {ChunkRegistry} chunkRegistry
   * @param {'destroy'|'create'|'select'|'none'} toolMode
   * @param {'destroy'|'copy'|'paste'|null} [subMode=null] - Sub-mode for select tool
   * @returns {{x: number, y: number, z: number}|null} Target position (placement for create/paste, hit for destroy/copy)
   */
  raycastTarget(camera, chunkRegistry, toolMode, subMode = null) {
    if (toolMode === 'none' || !camera) return null

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

    const hit = this.#castRay(originX, originY, originZ, dirX, dirY, dirZ, chunkRegistry, toolMode, subMode)

    if (!hit) return null

    // Return appropriate target based on mode
    // For create mode or paste sub-mode, return placement position (empty voxel)
    if (toolMode === 'create' || (toolMode === 'select' && subMode === 'paste')) {
      return { x: hit.placementX, y: hit.placementY, z: hit.placementZ }
    } else {
      return { x: hit.x, y: hit.y, z: hit.z }
    }
  }

  /**
   * Cast a ray through the voxel grid using 3D DDA algorithm.
   * For DESTROY/SELECT tool (except paste): returns the first non-air voxel.
   * For CREATE tool or paste mode: returns the empty voxel adjacent to the first non-air voxel
   * (the one the ray entered from - closest to player along the ray).
   * @private
   * @param {number} originX
   * @param {number} originY
   * @param {number} originZ
   * @param {number} dirX
   * @param {number} dirY
   * @param {number} dirZ
   * @param {ChunkRegistry} chunkRegistry
   * @param {'destroy'|'create'|'select'|'none'} toolMode
   * @param {'destroy'|'copy'|'paste'|null} [subMode=null] - Sub-mode for select tool
   * @returns {{x: number, y: number, z: number, placementX: number, placementY: number, placementZ: number}|null}
   */
  #castRay(originX, originY, originZ, dirX, dirY, dirZ, chunkRegistry, toolMode, subMode = null) {
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

    // Track if we've moved at least one step (to avoid returning start position)
    let hasMoved = false

    // Maximum travel distance
    const maxT = MAX_REACH_DISTANCE

    // Step size (voxel-sized steps along ray)
    const stepSize = 0.5  // Check every 0.5 voxels to ensure we don't miss any

    while (t < maxT) {
      // Check current voxel
      const voxel = this.#getVoxelAt(vx, vy, vz, chunkRegistry)

      if (voxel !== null && voxel !== VoxelType.AIR) {
        // Found a solid voxel
        // For destroy/copy: target the solid voxel itself
        // For create/paste: target the empty voxel before it (placement)
        const targetsSolid = (toolMode === 'destroy') || 
                             (toolMode === 'select' && subMode !== 'paste')
        
        if (targetsSolid) {
          // DESTROY/COPY mode: highlight the solid voxel itself
          return { x: vx, y: vy, z: vz, placementX: vx, placementY: vy, placementZ: vz }
        } else if (hasMoved) {
          // CREATE/PASTE mode: highlight the previous (empty) voxel we came from
          // Only if we've moved at least one step from the start
          return { x: prevX, y: prevY, z: prevZ, placementX: prevX, placementY: prevY, placementZ: prevZ }
        }
        // For CREATE/PASTE mode: if we haven't moved yet, continue stepping to find a valid placement
      }

      // Current voxel is empty, remember it as the previous one
      prevX = vx
      prevY = vy
      prevZ = vz
      hasMoved = true

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
   * Create the builder mode axis-aligned gizmo (hidden by default).
   * @private
   */
  #createBuilderGizmo() {
    const geometryX = new THREE.BoxGeometry(16, 0.05, 0.05)
    const geometryY = new THREE.BoxGeometry(0.05, 16, 0.05)
    const geometryZ = new THREE.BoxGeometry(0.05, 0.05, 16)

    const material = new THREE.MeshBasicMaterial({ color: this.#currentColor, transparent: true, opacity: 0.5, depthWrite: false })

    const meshX = new THREE.Mesh(geometryX, material)
    const meshY = new THREE.Mesh(geometryY, material)
    const meshZ = new THREE.Mesh(geometryZ, material)

    this.#builderGizmo = new THREE.Group()
    this.#builderGizmo.add(meshX)
    this.#builderGizmo.add(meshY)
    this.#builderGizmo.add(meshZ)
    this.#builderGizmo.visible = false
    this.#scene.add(this.#builderGizmo)
  }

  /**
   * Show highlight at specified voxel coordinates.
   * @private
   * @param {number} vx
   * @param {number} vy
   * @param {number} vz
   */
  #showAt(vx, vy, vz) {
    if (!this.#highlightMesh) return
    this.#highlightMesh.position.set(vx + 0.5, vy + 0.5, vz + 0.5)
    this.#highlightMesh.visible = true
    this.#isVisible = true

    if (this.#builderGizmo) {
      this.#builderGizmo.position.set(vx + 0.5, vy + 0.5, vz + 0.5)
      this.#builderGizmo.visible = this.#isBuilderMode
    }
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
    if (this.#builderGizmo) {
      this.#builderGizmo.visible = false
    }
  }

  /**
   * Get the currently highlighted voxel coordinates (for destroy mode).
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
   * Set preview voxel positions for multi-voxel preview (e.g., paste mode).
   * Creates/updates semi-transparent boxes at each position.
   * @param {Array<{x: number, y: number, z: number}>} positions
   * @param {number} color - Hex color value (0xRRGGBB)
   */
  setPreviewVoxels(positions, color) {
    // Clear existing preview meshes
    this.#clearPreviewMeshes()

    if (!positions || positions.length === 0) return

    // Create a mesh for each preview position
    const geometry = new THREE.BoxGeometry(1.05, 1.05, 1.05)
    const material = new THREE.MeshBasicMaterial({
      color: color,
      transparent: true,
      opacity: 0.3,
      depthWrite: false
    })

    for (const pos of positions) {
      const mesh = new THREE.Mesh(geometry, material)
      mesh.position.set(pos.x + 0.5, pos.y + 0.5, pos.z + 0.5)
      this.#scene.add(mesh)
      this.#previewMeshes.push(mesh)
    }
  }

  /**
   * Clear all preview meshes.
   * @private
   */
  #clearPreviewMeshes() {
    for (const mesh of this.#previewMeshes) {
      this.#scene.remove(mesh)
      mesh.geometry.dispose()
    }
    this.#previewMeshes = []
  }

  /**
   * Dispose of highlight resources.
   */
  dispose() {
    if (this.#highlightMesh) {
      this.#scene.remove(this.#highlightMesh)
      this.#highlightMesh.geometry.dispose()
      this.#highlightMesh.material.dispose()
      this.#highlightMesh = null
    }
    if (this.#builderGizmo) {
      this.#scene.remove(this.#builderGizmo)
      this.#builderGizmo.children.forEach((child) => {
        child.geometry.dispose()
      })
      // All children share the same material, dispose once
      if (this.#builderGizmo.children.length > 0) {
        this.#builderGizmo.children[0].material.dispose()
      }
      this.#builderGizmo = null
    }
    this.#clearPreviewMeshes()
  }
}
