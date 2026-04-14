// @ts-check
import * as THREE from 'three'
import { SUBVOXEL_SIZE } from './types.js'

/** @typedef {import('./EntityRegistry.js').EntityRegistry} EntityRegistry */
/** @typedef {import('./entities/BaseEntity.js').BaseEntity} BaseEntity */

/**
 * @class DebugVisualizer
 * @description Visualizes entity bounding boxes and other debug info when in debug mode.
 * Creates wireframe boxes around entities showing their server-side collision bounds.
 */
export class DebugVisualizer {
  /** @type {THREE.Scene} */
  #scene

  /** @type {EntityRegistry|null} */
  #entityRegistry = null

  /** @type {Map<GlobalEntityId, THREE.LineSegments>} */
  #boundingBoxes = new Map()

  /** @type {boolean} */
  #enabled = false

  /** @type {THREE.BoxGeometry} */
  #boxGeometry

  /** @type {THREE.LineBasicMaterial} */
  #boxMaterial

  /**
   * @param {THREE.Scene} scene
   * @param {boolean} enabled - Whether to start enabled
   */
  constructor(scene, enabled = false) {
    this.#scene = scene
    this.#enabled = enabled

    // Shared geometry and material for all bounding boxes
    // BoxGeometry is centered at origin, size 1x1x1 (we'll scale it)
    this.#boxGeometry = new THREE.BoxGeometry(1, 1, 1)
    this.#boxMaterial = new THREE.LineBasicMaterial({
      color: 0x00ff00,  // Green wireframe
      transparent: true,
      opacity: 0.8,
      depthTest: false,  // Always visible through other geometry
    })
  }

  /**
   * Set the entity registry to track entities from.
   * @param {EntityRegistry} registry
   */
  setEntityRegistry(registry) {
    this.#entityRegistry = registry
  }

  /**
   * Enable or disable debug visualization.
   * @param {boolean} enabled
   */
  setEnabled(enabled) {
    if (this.#enabled === enabled) return
    this.#enabled = enabled

    if (!enabled) {
      this.clear()
    }
  }

  /**
   * Check if visualization is enabled.
   * @returns {boolean}
   */
  isEnabled() {
    return this.#enabled
  }

  /**
   * Update all debug visualizations. Call once per frame.
   */
  update() {
    if (!this.#enabled || !this.#entityRegistry) {
      this.clear()
      return
    }

    const currentIds = new Set()

    // Update or create bounding boxes for all entities
    for (const entity of this.#entityRegistry.all()) {
      const id = entity.id
      currentIds.add(id)

      const bbox = entity.getBoundingBox()
      // Skip entities with no bounding box
      if (bbox.hx === 0 && bbox.hy === 0 && bbox.hz === 0) {
        continue
      }

      const pos = entity.currentPos

      let wireframe = this.#boundingBoxes.get(id)
      if (!wireframe) {
        wireframe = this.#createBoundingBox()
        this.#boundingBoxes.set(id, wireframe)
        this.#scene.add(wireframe)
      }

      // Update position and scale
      // Position is at center of entity
      const centerX = pos.x / SUBVOXEL_SIZE
      const centerY = pos.y / SUBVOXEL_SIZE
      const centerZ = pos.z / SUBVOXEL_SIZE

      // Scale to match bounding box size (2 * half-extent = full size)
      const scaleX = (bbox.hx * 2) / SUBVOXEL_SIZE
      const scaleY = (bbox.hy * 2) / SUBVOXEL_SIZE
      const scaleZ = (bbox.hz * 2) / SUBVOXEL_SIZE

      wireframe.position.set(centerX, centerY, centerZ)
      wireframe.scale.set(scaleX, scaleY, scaleZ)
    }

    // Remove bounding boxes for deleted entities
    for (const [id, wireframe] of this.#boundingBoxes) {
      if (!currentIds.has(id)) {
        this.#scene.remove(wireframe)
        wireframe.geometry.dispose()
        this.#boundingBoxes.delete(id)
      }
    }

    // Attack ray visualization removed
  }

  /**
   * Create a wireframe bounding box mesh.
   * @private
   * @returns {THREE.LineSegments}
   */
  #createBoundingBox() {
    const edges = new THREE.EdgesGeometry(this.#boxGeometry)
    const line = new THREE.LineSegments(edges, this.#boxMaterial)
    return line
  }

  /**
   * Clear all debug visualizations.
   */
  clear() {
    for (const [id, wireframe] of this.#boundingBoxes) {
      this.#scene.remove(wireframe)
      wireframe.geometry.dispose()
    }
    this.#boundingBoxes.clear()
  }

  /**
   * Dispose of all resources.
   */
  dispose() {
    this.clear()
    this.#boxMaterial.dispose()
    this.#boxGeometry.dispose()
  }

  // ── Attack Ray Visualization removed ──────────────────────────────────────
}
