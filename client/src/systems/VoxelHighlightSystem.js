// @ts-check
import * as THREE from 'three'
import { VoxelType, chunkIdFromVoxelPos, CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z, getChunkPos } from '../types.js'

/** @typedef {import('../types.js').ChunkId} ChunkId */
/** @typedef {import('../ChunkRegistry.js').ChunkRegistry} ChunkRegistry */

/** Maximum reach distance for voxel highlighting (in voxels) */
const MAX_REACH_DISTANCE = 5

/** Highlight color - yellow */
const HIGHLIGHT_COLOR = 0xFFFF00

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

  /**
   * @param {THREE.Scene} scene
   */
  constructor(scene) {
    this.#scene = scene
    this.#createHighlightMesh()
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
      color: HIGHLIGHT_COLOR,
      linewidth: 2 
    })
    
    this.#highlightMesh = new THREE.LineSegments(edges, material)
    this.#highlightMesh.visible = false
    this.#scene.add(this.#highlightMesh)
  }

  /**
   * Update the highlight based on camera position and selected tool.
   * @param {THREE.PerspectiveCamera} camera
   * @param {ChunkRegistry} chunkRegistry
   * @param {number} selectedToolIndex - Current hotbar selection (0-9)
   */
  update(camera, chunkRegistry, selectedToolIndex) {
    // Only show highlight for "Destroy Voxel" tool (slot index 2)
    if (selectedToolIndex !== 2) {
      this.#hide()
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
    const hit = this.#castRay(originX, originY, originZ, dirX, dirY, dirZ, chunkRegistry)

    if (hit) {
      this.#showAt(hit.x, hit.y, hit.z)
    } else {
      this.#hide()
    }
  }

  /**
   * Cast a ray through the voxel grid using 3D DDA algorithm.
   * Returns the first non-air voxel within reach, or null if none found.
   * @private
   * @param {number} originX
   * @param {number} originY
   * @param {number} originZ
   * @param {number} dirX
   * @param {number} dirY
   * @param {number} dirZ
   * @param {ChunkRegistry} chunkRegistry
   * @returns {{x: number, y: number, z: number}|null}
   */
  #castRay(originX, originY, originZ, dirX, dirY, dirZ, chunkRegistry) {
    // Normalize direction
    const len = Math.sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ)
    const dx = dirX / len
    const dy = dirY / len
    const dz = dirZ / len

    // Current voxel position
    let vx = Math.floor(originX)
    let vy = Math.floor(originY)
    let vz = Math.floor(originZ)

    // Step direction (-1 or 1)
    const stepX = dx > 0 ? 1 : -1
    const stepY = dy > 0 ? 1 : -1
    const stepZ = dz > 0 ? 1 : -1

    // Distance to next voxel boundary
    const nextX = dx > 0 ? (vx + 1 - originX) / dx : (vx - originX) / dx
    const nextY = dy > 0 ? (vy + 1 - originY) / dy : (vy - originY) / dy
    const nextZ = dz > 0 ? (vz + 1 - originZ) / dz : (vz - originZ) / dz

    // Delta T between voxel boundaries
    const deltaX = Math.abs(1 / dx)
    const deltaY = Math.abs(1 / dy)
    const deltaZ = Math.abs(1 / dz)

    let tX = nextX
    let tY = nextY
    let tZ = nextZ

    // Check initial voxel (offset slightly to avoid self-intersection with camera voxel)
    const startOffset = 0.01
    const startX = Math.floor(originX + dx * startOffset)
    const startY = Math.floor(originY + dy * startOffset)
    const startZ = Math.floor(originZ + dz * startOffset)

    // Track visited voxels to avoid checking the same one twice
    let currX = startX, currY = startY, currZ = startZ

    // Maximum travel distance
    const maxT = MAX_REACH_DISTANCE

    while (true) {
      // Check if we've exceeded reach
      const minT = Math.min(tX, tY, tZ)
      if (minT > maxT) break

      // Check current voxel
      const voxel = this.#getVoxelAt(currX, currY, currZ, chunkRegistry)
      if (voxel !== null && voxel !== VoxelType.AIR) {
        return { x: currX, y: currY, z: currZ }
      }

      // Step to next voxel
      if (tX < tY && tX < tZ) {
        currX += stepX
        tX += deltaX
      } else if (tY < tZ) {
        currY += stepY
        tY += deltaY
      } else {
        currZ += stepZ
        tZ += deltaZ
      }
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
   */
  #showAt(vx, vy, vz) {
    if (!this.#highlightMesh) return
    this.#highlightMesh.position.set(vx + 0.5, vy + 0.5, vz + 0.5)
    this.#highlightMesh.visible = true
    this.#isVisible = true
    this.#highlightedVoxel = { x: vx, y: vy, z: vz }
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
  }

  /**
   * Get the currently highlighted voxel coordinates.
   * @returns {{x: number, y: number, z: number}|null} Null if no voxel is highlighted.
   */
  getHighlightedVoxel() {
    return this.#highlightedVoxel
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
  }
}
