// @ts-check
import { BaseEntity } from './BaseEntity.js'
import { EntityType, SUBVOXEL_SIZE } from '../types.js'
import * as THREE from 'three'

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class PlayerEntity
 * @extends BaseEntity
 * @description Client-side player entity with a simple mesh representation.
 * Mesh structure:
 *   - Body: colored box (0.8 x 1.8 x 0.8) - matches server bounding box
 *   - Head: smaller box (0.5 x 0.5 x 0.5) offset up
 *   - Direction indicator: shows facing direction
 */
export class PlayerEntity extends BaseEntity {
  /** @type {THREE.Group|null} */
  mesh = null

  /** @type {number} PlayerId (uint32) - set if known, 0 otherwise */
  playerId = 0

  /**
   * @param {number} globalId  GlobalEntityId.
   * @param {number} entityType  EntityType (PLAYER or GHOST_PLAYER).
   * @param {THREE.Scene} scene  Three.js scene to add mesh to.
   */
  constructor(globalId, entityType, scene) {
    super(globalId, entityType)
    this.mesh = this.#createMesh(entityType)
    scene.add(this.mesh)
  }

  /**
   * Create the player mesh hierarchy.
   * @param {number} entityType
   * @returns {THREE.Group}
   */
  #createMesh(entityType) {
    const group = new THREE.Group()

    // Color based on entity type
    const isGhost = entityType === EntityType.GHOST_PLAYER
    const bodyColor = isGhost ? 0x88ccff : 0xff6600  // Light blue for ghost, orange for player
    const headColor = isGhost ? 0xaaddff : 0xff8844

    const bodyMat = new THREE.MeshLambertMaterial({ color: bodyColor })
    const headMat = new THREE.MeshLambertMaterial({ color: headColor })
    const faceMat = new THREE.MeshBasicMaterial({ color: 0x333333 })

    // Body: matches server BoundingBoxComponent (0.8 x 1.8 x 0.8 voxels)
    const bodyGeo = new THREE.BoxGeometry(0.8, 1.8, 0.8)
    const body = new THREE.Mesh(bodyGeo, bodyMat)
    body.position.y = 0.9  // Centered vertically (half height)
    body.castShadow = true
    group.add(body)

    // Head
    const headGeo = new THREE.BoxGeometry(0.5, 0.5, 0.5)
    const head = new THREE.Mesh(headGeo, headMat)
    head.position.y = 2.0  // On top of body
    head.castShadow = true
    group.add(head)

    // Face direction indicator (small dark box on front of head)
    const faceGeo = new THREE.BoxGeometry(0.2, 0.15, 0.05)
    const face = new THREE.Mesh(faceGeo, faceMat)
    face.position.set(0, 2.0, 0.28)  // Front of head
    group.add(face)

    return group
  }

  /**
   * Update animation and position.
   * @param {number} dt  Delta time in seconds.
   */
  updateAnimation(dt) {
    if (!this.mesh) return

    // Update position from motion component
    const gameClient = /** @type {any} */ (window).gameClient
    const tick = gameClient?.tick ?? 0
    const pos = this.predictAt(tick)
    this.mesh.position.set(
      pos.x / SUBVOXEL_SIZE,
      pos.y / SUBVOXEL_SIZE,
      pos.z / SUBVOXEL_SIZE
    )
  }

  /**
   * Remove mesh from scene.
   * @param {THREE.Scene} scene
   */
  destroy(scene) {
    if (this.mesh) {
      scene.remove(this.mesh)
      // Dispose geometries and materials
      this.mesh.traverse((child) => {
        if (child.geometry) child.geometry.dispose()
        if (child.material) {
          if (Array.isArray(child.material)) {
            child.material.forEach(m => m.dispose())
          } else {
            child.material.dispose()
          }
        }
      })
      this.mesh = null
    }
  }
}
