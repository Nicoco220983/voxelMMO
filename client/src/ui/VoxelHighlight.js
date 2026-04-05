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
 * Now also manages mode state:
 * - NORMAL mode: raycast-based targeting from camera center
 * - BUILDER mode: keyboard-controlled voxel target at fixed position
 * 
 * Use setMode() to switch modes, and getTarget() to get the current target.
 */
export class VoxelHighlight {
  /** @type {THREE.Scene} */
  #scene

  /** @type {THREE.Mesh|null} */
  #highlightMesh = null

  /** @type {boolean} */
  #isVisible = false

  /** @type {THREE.Group|null} */
  #builderGizmo = null

  /** Current target voxel position
   * @type {{x: number, y: number, z: number}|null}
   */
  #target = null

  /** Target type: 'surface' = hit voxel (for destroy/copy), 'adjacent' = empty voxel next to hit (for create/paste)
   * @type {'surface'|'adjacent'|null}
   */
  #targetType = null

  /** @type {number} Current highlight color */
  #currentColor = 0xFFFFFF

  /** Preview meshes for multi-voxel preview (e.g., paste mode)
   * @type {THREE.Mesh[]}
   */
  #previewMeshes = []

  // ── Mode state ────────────────────────────────────────────────────────────

  /** Normal mode: raycast-based targeting */
  static NORMAL = 'normal'
  /** Builder mode: keyboard-controlled voxel target */
  static BUILDER = 'builder'

  /** @type {'normal'|'builder'} */
  #mode = VoxelHighlight.NORMAL



  /**
   * @param {THREE.Scene} scene
   */
  constructor(scene) {
    this.#scene = scene
    this.#createHighlightMesh()
    this.#createBuilderGizmo()
  }

  // ── Mode management ───────────────────────────────────────────────────────

  /**
   * Set the targeting mode.
   * @param {'normal'|'builder'} mode
   */
  setMode(mode) {
    this.#mode = mode
  }

  /**
   * Get the current targeting mode.
   * @returns {'normal'|'builder'}
   */
  getMode() {
    return this.#mode
  }

  /**
   * Check if currently in builder mode.
   * @returns {boolean}
   */
  isBuilderMode() {
    return this.#mode === VoxelHighlight.BUILDER
  }

  /**
   * Move builder target by delta (builder mode only).
   * @param {number} dx
   * @param {number} dy
   * @param {number} dz
   */
  moveBuilderTarget(dx, dy, dz) {
    if (!this.#target) return
    const adx = Math.abs(dx)
    const ady = Math.abs(dy)
    const adz = Math.abs(dz)
    // Builder mode: only move along one axis at a time. Priority: Y > Z > X.
    if (ady >= adx && ady >= adz) {
      this.#target.y += Math.sign(dy)
    } else if (adz >= adx && adz >= ady) {
      this.#target.z += Math.sign(dz)
    } else if (adx > 0) {
      this.#target.x += Math.sign(dx)
    }
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
   * Get the current target based on mode.
   * In builder mode: returns the builder target (moved via keyboard).
   * In normal mode: performs raycast from camera.
   * @param {import('three').PerspectiveCamera} camera
   * @param {ChunkRegistry} chunkRegistry
   * @param {'destroy'|'create'|'select'|'none'} toolMode
   * @param {'destroy'|'copy'|'paste'|null} [subMode=null] - Sub-mode for select tool
   * @returns {{x: number, y: number, z: number}|null}
   */
  getTarget(camera, chunkRegistry, toolMode, subMode = null) {
    // No tool selected - return null even in builder mode
    if (toolMode === 'none') {
      return null
    }

    if (this.#mode === VoxelHighlight.BUILDER) {
      // Builder mode: return the current target (keyboard-controlled)
      return this.#target
    }

    // Normal mode: do raycast to get target
    return this.raycastTarget(camera, chunkRegistry, toolMode, subMode)
  }

  /**
   * Initialize builder mode with a starting position.
   * Called when entering builder mode - uses current raycast result as starting point.
   * @param {{x: number, y: number, z: number}|null} startPosition
   */
  initBuilderTarget(startPosition) {
    if (startPosition) {
      this.#target = startPosition
    }
  }

  /**
   * Get the current target type ('surface' = hit voxel, 'adjacent' = empty voxel next to hit).
   * @returns {'surface'|'adjacent'|null}
   */
  getTargetType() {
    return this.#targetType
  }

  /**
   * Set the current target for visualization.
   * Called by main loop with target from getTarget().
   * @param {{x: number, y: number, z: number}|null} target
   * @param {number} color - Hex color value (0xRRGGBB), 0 to hide
   * @param {'destroy'|'create'|'select'} toolMode - Tool mode for tracking voxel type
   * @param {'destroy'|'copy'|'paste'|null} [subMode=null] - Sub-mode for select tool
   */
  setTarget(target, color, toolMode, subMode = null) {
    // Update color if changed
    if (this.#currentColor !== color) {
      this.#currentColor = color
      this.#updateHighlightColor(color)
    }

    if (!target || color === 0) {
      this.#hide()
      return
    }

    // Track target type based on tool mode:
    // - 'adjacent': create mode, paste sub-mode (empty voxel next to hit)
    // - 'surface': destroy, copy, and other modes (the hit voxel itself)
    if (toolMode === 'create' || (toolMode === 'select' && subMode === 'paste')) {
      this.#targetType = 'adjacent'
    } else {
      this.#targetType = 'surface'
    }
    
    this.#target = target

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
      this.#builderGizmo.visible = this.isBuilderMode()
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
    this.#target = null
    this.#targetType = null
    if (this.#builderGizmo) {
      this.#builderGizmo.visible = false
    }
    this.#clearPreviewMeshes()
  }

  /**
   * Get the current target position.
   * The target is always the correct position for the current tool mode:
   * - 'surface' type: the hit voxel itself (for destroy/copy)
   * - 'adjacent' type: empty voxel adjacent to hit (for create/paste)
   * @returns {{x: number, y: number, z: number}|null}
   */
  getCurrentTarget() {
    return this.#target
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
