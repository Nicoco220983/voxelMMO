// @ts-check
import * as THREE from 'three'

/**
 * Encapsulates the state, logic and visuals of a bulk voxel selection.
 * Owns its own Three.js mesh and handles the start/end state machine.
 */
export class BulkVoxelsSelection {
  /** @type {THREE.Scene} */
  #scene

  /** @type {THREE.Mesh|null} */
  #mesh = null

  /** @type {boolean} */
  #active = false

  /** @type {{x: number, y: number, z: number}|null} */
  #start = null

  /** @type {number} Current highlight color */
  #color = 0xFFFFFF

  /**
   * @param {THREE.Scene} scene
   */
  constructor(scene) {
    this.#scene = scene
    this.#createMesh()
  }

  /**
   * @private
   */
  #createMesh() {
    const geometry = new THREE.BoxGeometry(1, 1, 1)
    const material = new THREE.MeshBasicMaterial({
      color: this.#color,
      transparent: true,
      opacity: 0.25,
      depthWrite: false
    })
    this.#mesh = new THREE.Mesh(geometry, material)
    this.#mesh.visible = false
    this.#scene.add(this.#mesh)
  }

  /**
   * True if bulk selection is currently active.
   * @returns {boolean}
   */
  isActive() {
    return this.#active
  }

  /**
   * Current bulk phase matching BaseController semantics.
   * @returns {'idle'|'selecting_start'|'selecting_end'}
   */
  getBulkPhase() {
    if (!this.#active) return 'idle'
    return this.#start ? 'selecting_end' : 'selecting_start'
  }

  /**
   * @returns {{x: number, y: number, z: number}|null}
   */
  getStartPos() {
    return this.#start
  }

  /**
   * Activate bulk mode without setting a start voxel yet (e.g. triple-tap).
   * @param {number} [color=0xFF0000] - Hex color value (0xRRGGBB)
   */
  activate(color = 0xFF0000) {
    this.#active = true
    this.#start = null
    this.setColor(color)
    if (this.#mesh) this.#mesh.visible = false
  }

  /**
   * Activate bulk mode and immediately set the start voxel (e.g. long-press).
   * @param {{x: number, y: number, z: number}} startPos
   * @param {number} [color=0xFF0000] - Hex color value (0xRRGGBB)
   */
  start(startPos, color = 0xFF0000) {
    this.#active = true
    this.#start = { x: startPos.x, y: startPos.y, z: startPos.z }
    this.setColor(color)
    this.#updateMesh(this.#start, this.#start)
  }

  /**
   * Exit bulk mode and hide visuals.
   */
  exit() {
    this.#active = false
    this.#start = null
    if (this.#mesh) this.#mesh.visible = false
  }

  /**
   * Handle an action click while bulk mode is active.
   * @param {{x: number, y: number, z: number}|null} targetPos
   * @returns {{consumed: boolean, complete: boolean, start: {x: number, y: number, z: number}|null, end: {x: number, y: number, z: number}|null}}
   */
  onAction(targetPos) {
    if (!this.#active) {
      return { consumed: false, complete: false, start: null, end: null }
    }

    if (!this.#start) {
      if (!targetPos) {
        return { consumed: true, complete: false, start: null, end: null }
      }
      this.#start = { x: targetPos.x, y: targetPos.y, z: targetPos.z }
      this.#updateMesh(this.#start, this.#start)
      return { consumed: true, complete: false, start: this.#start, end: null }
    }

    const start = this.#start
    const end = targetPos ? { x: targetPos.x, y: targetPos.y, z: targetPos.z } : null
    this.#active = false
    this.#start = null
    if (this.#mesh) this.#mesh.visible = false
    return { consumed: true, complete: true, start, end }
  }

  /**
   * Update the dynamic end position for the visual preview.
   * @param {{x: number, y: number, z: number}|null} endPos
   */
  updateEnd(endPos) {
    if (!this.#active || !this.#start || !endPos || !this.#mesh) return
    this.#updateMesh(this.#start, endPos)
  }

  /**
   * Update the highlight color.
   * @param {number} color - Hex color value (0xRRGGBB)
   */
  setColor(color) {
    this.#color = color
    if (this.#mesh) {
      const material = /** @type {THREE.MeshBasicMaterial} */ (this.#mesh.material)
      material.color.setHex(color)
    }
  }

  /**
   * @private
   * @param {{x: number, y: number, z: number}} start
   * @param {{x: number, y: number, z: number}} end
   */
  #updateMesh(start, end) {
    const minX = Math.min(start.x, end.x)
    const maxX = Math.max(start.x, end.x)
    const minY = Math.min(start.y, end.y)
    const maxY = Math.max(start.y, end.y)
    const minZ = Math.min(start.z, end.z)
    const maxZ = Math.max(start.z, end.z)

    const sizeX = maxX - minX + 1.05
    const sizeY = maxY - minY + 1.05
    const sizeZ = maxZ - minZ + 1.05

    const centerX = (minX + maxX) / 2 + 0.5
    const centerY = (minY + maxY) / 2 + 0.5
    const centerZ = (minZ + maxZ) / 2 + 0.5

    this.#mesh.position.set(centerX, centerY, centerZ)
    this.#mesh.scale.set(sizeX, sizeY, sizeZ)
    this.#mesh.visible = true
  }

  /**
   * Dispose of Three.js resources.
   */
  dispose() {
    if (this.#mesh) {
      this.#scene.remove(this.#mesh)
      this.#mesh.geometry.dispose()
      this.#mesh.material.dispose()
      this.#mesh = null
    }
  }
}
