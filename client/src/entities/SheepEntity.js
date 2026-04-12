// @ts-check
import { BaseEntity } from './BaseEntity.js'
import { EntityType, SUBVOXEL_SIZE, POSITION_BIT, AI_BEHAVIOR_BIT, HEALTH_BIT } from '../types.js'
import { DynamicPositionComponent } from '../components/DynamicPositionComponent.js'
import { SheepBehaviorComponent } from '../components/SheepBehaviorComponent.js'
import { HealthComponent } from '../components/HealthComponent.js'
import * as THREE from 'three'

/** @typedef {import('../types.js').GlobalEntityId} GlobalEntityId */
/** @typedef {import('../types.js').ChunkId} ChunkId */
/** @typedef {import('../utils.js').BufReader} BufReader */
/** @typedef {import('../ChunkRegistry.js').ChunkRegistry} ChunkRegistry */
/** @typedef {import('../EntityRegistry.js').EntityRegistry} EntityRegistry */

/**
 * @class SheepEntity
 * @extends BaseEntity
 * @description Client-side sheep entity with simple procedural mesh and animation.
 *
 * Mesh origin is at the bounding box center (y=0), matching server convention.
 * Sheep half-height is 0.5 voxels, so feet are at y=-0.5.
 *
 * Mesh structure:
 *   - Body: white box (0.7 x 0.5 x 1.0) centered at y=0
 *   - Head: white box (0.35 x 0.35 x 0.4) at y=0.25, offset +Z
 *   - Legs: 4 small boxes (0.15 x 0.4 x 0.15) with pivot at y=-0.25, feet at y=-0.65
 *   - Eyes: small black boxes at y=0.3
 */
export class SheepEntity extends BaseEntity {
  /** @type {SheepBehaviorComponent} */
  behavior = new SheepBehaviorComponent()

  /** @type {HealthComponent} Health component for damage/death tracking */
  health = new HealthComponent()

  /** @type {THREE.Group|null} */
  mesh = null

  /** @type {THREE.Mesh[]} */
  legs = []

  /** @type {number} Animation time accumulator */
  animTime = 0

  /**
   * @param {GlobalEntityId} globalId  GlobalEntityId.
   * @param {THREE.Scene} scene  Three.js scene to add mesh to.
   */
  constructor(globalId, scene) {
    super(globalId, EntityType.SHEEP)
    this.mesh = this.#createMesh()
    scene.add(this.mesh)
  }

  /**
   * Create the sheep mesh hierarchy.
   * @returns {THREE.Group}
   */
  #createMesh() {
    const group = new THREE.Group()

    const HEIGHT_OFFSET = 0.2

    // Materials
    const woolMat = new THREE.MeshLambertMaterial({ color: 0xeeeeee })
    const faceMat = new THREE.MeshBasicMaterial({ color: 0x333333 })

    // Body - centered at y=0 to match server bounding box center
    // Server position is at center of bbox; sheep half-height is 0.5 voxels
    const bodyGeo = new THREE.BoxGeometry(0.7, 0.5, 1.0)
    const body = new THREE.Mesh(bodyGeo, woolMat)
    body.position.y = HEIGHT_OFFSET
    body.castShadow = true
    body.receiveShadow = true
    group.add(body)

    // Head
    const headGeo = new THREE.BoxGeometry(0.35, 0.35, 0.4)
    const head = new THREE.Mesh(headGeo, woolMat)
    head.position.set(0, 0.25 + HEIGHT_OFFSET, 0.6)
    head.castShadow = true
    head.receiveShadow = true
    group.add(head)

    // Eyes
    const eyeGeo = new THREE.BoxGeometry(0.06, 0.06, 0.02)
    const leftEye = new THREE.Mesh(eyeGeo, faceMat)
    leftEye.position.set(-0.1, 0.3 + HEIGHT_OFFSET, 0.81)
    group.add(leftEye)
    const rightEye = new THREE.Mesh(eyeGeo, faceMat)
    rightEye.position.set(0.1, 0.3 + HEIGHT_OFFSET, 0.81)
    group.add(rightEye)

    // Legs - stored for animation
    // Pivot groups allow rotation around the top of the leg
    // Legs extend from y=-0.25 down to y=-0.65 (0.4 length), feet at y=-0.65
    const legGeo = new THREE.BoxGeometry(0.15, 0.4, 0.15)
    const legPositions = [
      { x: -0.22, z: 0.35, name: 'FL' },  // Front left
      { x: 0.22, z: 0.35, name: 'FR' },   // Front right
      { x: -0.22, z: -0.35, name: 'BL' }, // Back left
      { x: 0.22, z: -0.35, name: 'BR' },  // Back right
    ]

    for (const pos of legPositions) {
      const legGroup = new THREE.Group()
      legGroup.position.set(pos.x, -0.25 + HEIGHT_OFFSET, pos.z)

      const leg = new THREE.Mesh(legGeo, woolMat)
      leg.position.y = -0.2  // Offset so pivot is at top
      leg.castShadow = true
      leg.receiveShadow = true

      legGroup.add(leg)
      group.add(legGroup)
      this.legs.push(legGroup)
    }

    return group
  }

  /**
   * Update animation state.
   * @param {number} dt  Delta time in seconds.
   */
  updateAnimation(dt) {
    if (!this.mesh) return

    // Update position from motion component (predicted position from PhysicsPredictionSystem)
    const pos = this.currentPos
    this.mesh.position.set(
      pos.x / SUBVOXEL_SIZE,
      pos.y / SUBVOXEL_SIZE,
      pos.z / SUBVOXEL_SIZE
    )

    // Update rotation from velocity (face movement direction)
    if (Math.abs(this.motion.receivedVx) > 1 || Math.abs(this.motion.receivedVz) > 1) {
      const targetYaw = Math.atan2(this.motion.receivedVx, this.motion.receivedVz)
      // Smooth rotation
      let yawDiff = targetYaw - this.mesh.rotation.y
      while (yawDiff > Math.PI) yawDiff -= Math.PI * 2
      while (yawDiff < -Math.PI) yawDiff += Math.PI * 2
      this.mesh.rotation.y += yawDiff * 0.1
    }

    // Animate legs when walking
    if (this.behavior.state === 1) { // WALKING
      this.animTime += dt * 8  // Speed of leg swing
      const swing = Math.sin(this.animTime)

      // Diagonal pairs swing together
      this.legs[0].rotation.x = swing * 0.4      // Front left
      this.legs[3].rotation.x = swing * 0.4      // Back right
      this.legs[1].rotation.x = -swing * 0.4     // Front right
      this.legs[2].rotation.x = -swing * 0.4     // Back left
    } else {
      // Return legs to neutral when idle
      this.animTime = 0
      for (const leg of this.legs) {
        leg.rotation.x *= 0.9  // Smoothly return to 0
      }
    }
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

  // ─── Static Deserialization Methods ───────────────────────────────────────

  /**
   * Deserialize sheep entity creation into an existing entity.
   * 
   * For CREATE_ENTITY: Reset ALL components to defaults first, then deserialize
   * only the components indicated by componentMask. Missing components remain
   * at their default values.
   * 
   * @param {SheepEntity} entity - The entity to deserialize into (already created)
   * @param {BufReader} reader
   * @param {number} componentMask - Bitmask indicating which components are present
   * @param {number} messageTick
   */
  static deserializeCreate(entity, reader, componentMask, messageTick) {
    if (entity) {
      // 1. Reset ALL components to defaults first
      entity.motion.resetToDefaults()
      entity.behavior.resetToDefaults()
      entity.health.resetToDefaults()
    }

    // 2. Deserialize only components indicated by mask (missing = stay at default)
    // If entity is null, component data is still read from reader but discarded
    SheepEntity.deserializeComponents(entity, reader, componentMask, messageTick)
  }

  /**
   * Deserialize sheep entity update.
   * @param {SheepEntity?} entity
   * @param {BufReader} reader
   * @param {number} componentMask
   * @param {number} messageTick
   */
  static deserializeUpdate(entity, reader, componentMask, messageTick) {
    SheepEntity.deserializeComponents(entity, reader, componentMask, messageTick)
  }

  /**
   * @param {SheepEntity?} self 
   * @param {BufReader} reader 
   * @param {number} componentMask 
   * @param {number} messageTick 
   */
  static deserializeComponents(self, reader, componentMask, messageTick) {
    if (componentMask & POSITION_BIT) DynamicPositionComponent.deserialize(self?.motion, reader, messageTick)
    if (componentMask & AI_BEHAVIOR_BIT) SheepBehaviorComponent.deserialize(self?.behavior, reader, messageTick)
    if (componentMask & HEALTH_BIT) HealthComponent.deserialize(self?.health, reader, messageTick)
  }
}
