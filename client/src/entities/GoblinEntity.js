// @ts-check
import { LivingEntity } from './LivingEntity.js'
import { EntityType } from '../EntityCatalog.js'
import { SUBVOXEL_SIZE } from '../types.js'
import { POSITION_BIT, AI_BEHAVIOR_BIT, HEALTH_BIT } from '../components/ComponentBits.js'
import { DynamicPositionComponent } from '../components/DynamicPositionComponent.js'
import { GoblinBehaviorComponent } from '../components/GoblinBehaviorComponent.js'
import { HealthComponent } from '../components/HealthComponent.js'

import * as THREE from 'three'

/** @typedef {import('../types.js').GlobalEntityId} GlobalEntityId */
/** @typedef {import('../types.js').ChunkId} ChunkId */
/** @typedef {import('../utils.js').BufReader} BufReader */
/** @typedef {import('../ChunkRegistry.js').ChunkRegistry} ChunkRegistry */
/** @typedef {import('../EntityRegistry.js').EntityRegistry} EntityRegistry */

/**
 * @class GoblinEntity
 * @extends BaseEntity
 * @description Client-side goblin entity with green skin, big nose and big ears.
 *
 * Mesh origin is at the bounding box center (y=0), matching server convention.
 * Goblin is smaller than player: 0.45 voxels half-height.
 *
 * Mesh structure:
 *   - Body: green box (0.5 x 0.45 x 0.4) centered at y=0
 *   - Head: green box (0.4 x 0.35 x 0.35) at y=0.2, with protruding nose
 *   - Nose: large green box (0.12 x 0.08 x 0.15) at front of face
 *   - Ears: large pointed ears on sides of head (0.08 x 0.2 x 0.1)
 *   - Legs: 2 boxes (0.12 x 0.35 x 0.12) with animation
 *   - Arms: 2 boxes (0.1 x 0.3 x 0.1) with attack animation
 *
 * States: 0 = IDLE, 1 = WALKING, 2 = CHASE, 3 = ATTACK
 */
/** Bounding box half-extents in sub-voxels (must match server GoblinEntity.hpp) */
const GOBLIN_BBOX_HX = 115   // 0.45 voxels
const GOBLIN_BBOX_HY = 179   // 0.7 voxels (taller for easier targeting)
const GOBLIN_BBOX_HZ = 115   // 0.45 voxels

export class GoblinEntity extends LivingEntity {
  /** @type {GoblinBehaviorComponent} */
  behavior = new GoblinBehaviorComponent()

  /**
   * Get bounding box half-extents in sub-voxels.
   * @returns {{hx: number, hy: number, hz: number}}
   */
  getBoundingBox() {
    return { hx: GOBLIN_BBOX_HX, hy: GOBLIN_BBOX_HY, hz: GOBLIN_BBOX_HZ }
  }



  /** @type {THREE.Group|null} */
  mesh = null

  /** @type {THREE.Mesh[]} */
  legs = []

  /** @type {THREE.Mesh[]} */
  arms = []

  /** @type {THREE.Mesh|null} */
  nose = null

  /** @type {number} Animation time accumulator */
  animTime = 0

  /** @type {number} Previous state for transition detection */
  prevState = 0

  /**
   * @param {GlobalEntityId} globalId  GlobalEntityId.
   * @param {THREE.Scene} scene  Three.js scene to add mesh to.
   */
  constructor(globalId, scene) {
    super(globalId, EntityType.GOBLIN)
    this.mesh = this.#createMesh()
    scene.add(this.mesh)
  }

  /**
   * Create the goblin mesh hierarchy.
   * @returns {THREE.Group}
   */
  #createMesh() {
    const group = new THREE.Group()

    const HEIGHT_OFFSET = 0.1

    // Materials - goblin green with darker green for details
    const skinMat = new THREE.MeshLambertMaterial({ color: 0x4a8c2a })  // Goblin green
    const darkMat = new THREE.MeshLambertMaterial({ color: 0x2d5a1a })  // Darker green
    const eyeMat = new THREE.MeshBasicMaterial({ color: 0xff0000 })      // Red eyes

    // Body - centered at y=0 (slightly smaller than player)
    const bodyGeo = new THREE.BoxGeometry(0.5, 0.45, 0.4)
    const body = new THREE.Mesh(bodyGeo, skinMat)
    body.position.y = 0.1 + HEIGHT_OFFSET
    body.castShadow = true
    body.receiveShadow = true
    group.add(body)

    // Head - positioned above body
    const headGeo = new THREE.BoxGeometry(0.4, 0.35, 0.35)
    const head = new THREE.Mesh(headGeo, skinMat)
    head.position.set(0, 0.5 + HEIGHT_OFFSET, 0)
    head.castShadow = true
    head.receiveShadow = true
    group.add(head)

    // BIG NOSE - protruding from face
    const noseGeo = new THREE.BoxGeometry(0.12, 0.1, 0.15)
    const nose = new THREE.Mesh(noseGeo, darkMat)
    nose.position.set(0, 0.48 + HEIGHT_OFFSET, 0.22)
    nose.castShadow = true
    group.add(nose)
    this.nose = nose

    // BIG EARS - large pointed ears on sides
    const earGeo = new THREE.BoxGeometry(0.08, 0.22, 0.12)
    
    // Left ear
    const leftEar = new THREE.Mesh(earGeo, skinMat)
    leftEar.position.set(-0.24, 0.55 + HEIGHT_OFFSET, 0)
    leftEar.rotation.z = 0.3  // Angle outward
    leftEar.castShadow = true
    group.add(leftEar)
    
    // Right ear
    const rightEar = new THREE.Mesh(earGeo, skinMat)
    rightEar.position.set(0.24, 0.55 + HEIGHT_OFFSET, 0)
    rightEar.rotation.z = -0.3  // Angle outward
    rightEar.castShadow = true
    group.add(rightEar)

    // Red eyes
    const eyeGeo = new THREE.BoxGeometry(0.06, 0.05, 0.02)
    const leftEye = new THREE.Mesh(eyeGeo, eyeMat)
    leftEye.position.set(-0.1, 0.52 + HEIGHT_OFFSET, 0.18)
    group.add(leftEye)
    const rightEye = new THREE.Mesh(eyeGeo, eyeMat)
    rightEye.position.set(0.1, 0.52 + HEIGHT_OFFSET, 0.18)
    group.add(rightEye)

    // Legs - stored for animation
    const legGeo = new THREE.BoxGeometry(0.12, 0.35, 0.12)
    const legPositions = [
      { x: -0.12, z: 0.1, name: 'L' },   // Left
      { x: 0.12, z: 0.1, name: 'R' },    // Right
    ]

    for (const pos of legPositions) {
      const legGroup = new THREE.Group()
      legGroup.position.set(pos.x, -0.12 + HEIGHT_OFFSET, pos.z)

      const leg = new THREE.Mesh(legGeo, darkMat)
      leg.position.y = -0.175  // Offset so pivot is at top
      leg.castShadow = true
      leg.receiveShadow = true

      legGroup.add(leg)
      group.add(legGroup)
      this.legs.push(legGroup)
    }

    // Arms - stored for attack animation
    const armGeo = new THREE.BoxGeometry(0.1, 0.3, 0.1)
    const armPositions = [
      { x: -0.32, z: 0, name: 'L' },   // Left
      { x: 0.32, z: 0, name: 'R' },    // Right
    ]

    for (const pos of armPositions) {
      const armGroup = new THREE.Group()
      armGroup.position.set(pos.x, 0.2 + HEIGHT_OFFSET, pos.z)

      const arm = new THREE.Mesh(armGeo, skinMat)
      arm.position.y = -0.15  // Offset so pivot is at shoulder
      arm.castShadow = true
      arm.receiveShadow = true

      armGroup.add(arm)
      group.add(armGroup)
      this.arms.push(armGroup)
    }

    return group
  }

  /**
   * Update animation state.
   * @param {number} dt  Delta time in seconds.
   */
  updateAnimation(dt) {
    if (!this.mesh) return

    // Check for damage and update flash effect
    this.checkDamage()
    this.updateDamageFlash(dt)

    // Update death rotation (fall over when health reaches 0)
    this.updateDeathRotation(dt)
    if (this.isDead) return

    // Update position from motion component
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
      this.mesh.rotation.y += yawDiff * 0.15
    }

    // State-based animation
    const state = this.behavior.state
    
    switch (state) {
      case 0: // IDLE
        this.#animateIdle(dt)
        break
      case 1: // WALKING
      case 2: // CHASE (faster walk)
        this.#animateWalk(dt, state === 2)
        break
      case 3: // ATTACK
        this.#animateAttack(dt)
        break
    }

    this.prevState = state
  }

  /**
   * Animate idle state.
   * @param {number} dt
   */
  #animateIdle(dt) {
    // Return limbs to neutral
    this.animTime = 0
    for (const leg of this.legs) {
      leg.rotation.x *= 0.9
    }
    for (const arm of this.arms) {
      arm.rotation.x *= 0.9
    }
    
    // Breathing animation
    const breath = Math.sin(Date.now() * 0.002) * 0.02
    if (this.mesh) {
      this.mesh.scale.y = 1 + breath
      this.mesh.scale.x = 1 - breath * 0.5
      this.mesh.scale.z = 1 - breath * 0.5
    }
  }

  /**
   * Animate walking/chasing state.
   * @param {number} dt
   * @param {boolean} isChase - Whether in chase mode (faster)
   */
  #animateWalk(dt, isChase) {
    const speed = isChase ? 12 : 8  // Faster animation when chasing
    this.animTime += dt * speed
    const swing = Math.sin(this.animTime)

    // Leg swing - alternating
    this.legs[0].rotation.x = swing * 0.5      // Left
    this.legs[1].rotation.x = -swing * 0.5     // Right

    if (isChase) {
      // CHASE: Both arms pointing toward sky (up)
      // Smoothly transition to arms up
      const targetArmRot = -Math.PI + 0.2  // Arms straight up, slightly angled
      this.arms[0].rotation.x += (targetArmRot - this.arms[0].rotation.x) * 0.2
      this.arms[1].rotation.x += (targetArmRot - this.arms[1].rotation.x) * 0.2
    } else {
      // WALKING: Arm swing - opposite to legs
      this.arms[0].rotation.x = -swing * 0.4     // Left
      this.arms[1].rotation.x = swing * 0.4      // Right
    }

    // Reset scale from breathing
    if (this.mesh) {
      this.mesh.scale.set(1, 1, 1)
    }
  }

  /**
   * Animate attack state.
   * @param {number} dt
   */
  #animateAttack(dt) {
    // Alternating arms striking from sky toward player
    const cycleDuration = 0.5  // 0.5s per arm cycle
    const time = Date.now() / 1000
    
    // Left arm leads by half cycle
    const leftPhase = (time % cycleDuration) / cycleDuration
    const rightPhase = ((time + cycleDuration / 2) % cycleDuration) / cycleDuration
    
    // Function to calculate arm rotation for a phase
    // 0.0-0.3: Arm up (sky), 0.3-0.6: Strike down toward player, 0.6-1.0: Return up
    const getArmRotation = (phase) => {
      const armUpRot = -Math.PI + 0.2      // Pointing to sky
      const armForwardRot = -Math.PI / 3   // Pointing forward/down toward player
      
      if (phase < 0.3) {
        // Hold up position
        return armUpRot
      } else if (phase < 0.6) {
        // Strike down
        const strikeProgress = (phase - 0.3) / 0.3
        return armUpRot + strikeProgress * (armForwardRot - armUpRot)
      } else {
        // Return up
        const returnProgress = (phase - 0.6) / 0.4
        return armForwardRot + returnProgress * (armUpRot - armForwardRot)
      }
    }
    
    this.arms[0].rotation.x = getArmRotation(leftPhase)
    this.arms[1].rotation.x = getArmRotation(rightPhase)
    
    // Subtle body lunge on each strike
    const leftStriking = leftPhase >= 0.3 && leftPhase < 0.5
    const rightStriking = rightPhase >= 0.3 && rightPhase < 0.5
    if ((leftStriking || rightStriking) && this.mesh) {
      this.mesh.position.y -= 0.02  // Slight dip during strike
    }
  }

  /**
   * Remove mesh from scene.
   * @param {THREE.Scene} scene
   */
  destroy(scene) {
    // Clean up damage flash materials first
    this._cleanupDamageFlash()

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
    super.destroy(scene)
  }

  // ─── Static Deserialization Methods ───────────────────────────────────────

  /**
   * Deserialize goblin entity creation into an existing entity.
   * 
   * For CREATE_ENTITY: Reset ALL components to defaults first, then deserialize
   * only the components indicated by componentMask. Missing components remain
   * at their default values.
   * 
   * @param {GoblinEntity} entity - The entity to deserialize into (already created)
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
      // Note: LivingEntity.health is reset above
    }

    // 2. Deserialize only components indicated by mask
    GoblinEntity.deserializeComponents(entity, reader, componentMask, messageTick)
  }

  /**
   * Deserialize goblin entity update.
   * @param {GoblinEntity?} entity
   * @param {BufReader} reader
   * @param {number} componentMask
   * @param {number} messageTick
   */
  static deserializeUpdate(entity, reader, componentMask, messageTick) {
    GoblinEntity.deserializeComponents(entity, reader, componentMask, messageTick)
  }

  /**
   * @param {GoblinEntity?} self 
   * @param {BufReader} reader 
   * @param {number} componentMask 
   * @param {number} messageTick 
   */
  static deserializeComponents(self, reader, componentMask, messageTick) {
    if (componentMask & POSITION_BIT) DynamicPositionComponent.deserialize(self?.motion, reader, messageTick)
    if (componentMask & AI_BEHAVIOR_BIT) GoblinBehaviorComponent.deserialize(self?.behavior, reader, messageTick)
    if (componentMask & HEALTH_BIT) HealthComponent.deserialize(self?.health, reader, messageTick)
  }
}
