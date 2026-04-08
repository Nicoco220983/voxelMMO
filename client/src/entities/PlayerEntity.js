// @ts-check
import { BaseEntity } from './BaseEntity.js'
import { EntityType, SUBVOXEL_SIZE, POSITION_BIT } from '../types.js'
import * as THREE from 'three'
import { DynamicPositionComponent } from '../components/DynamicPositionComponent.js'

/** @typedef {import('../utils.js').BufReader} BufReader */
/** @typedef {import('../types.js').GlobalEntityId} GlobalEntityId */
/** @typedef {import('../types.js').ChunkId} ChunkId */
/** @typedef {import('../types.js').EntityType} EntityType */
/** @typedef {import('../ChunkRegistry.js').ChunkRegistry} ChunkRegistry */
/** @typedef {import('../EntityRegistry.js').EntityRegistry} EntityRegistry */

/**
 * @class PlayerEntity
 * @extends BaseEntity
 * @description Client-side player entity with a Minecraft-style mesh.
 *
 * Server bounding box: 0.8 x 1.8 x 0.8 (half-extents: 0.4, 0.9, 0.4)
 * Mesh fits within ~1.6 voxels height (slightly smaller than bounding box for visual comfort).
 *
 * Mesh structure (base sizes, scaled by SIZE):
 *   - Head: 0.4 x 0.4 x 0.4
 *   - Body: 0.4 x 0.6 x 0.2
 *   - Arms: 0.15 x 0.6 x 0.15 each
 *   - Legs: 0.18 x 0.6 x 0.18 each
 *
 * The self player entity is invisible (mesh not added to scene).
 */
export class PlayerEntity extends BaseEntity {
  /** @type {THREE.Group|null} */
  mesh = null

  /** @type {number} PlayerId (uint32) - set if known, 0 otherwise */
  playerId = 0

  /** @type {boolean} True if this is the local player (self) */
  isSelf = false

  /** @type {THREE.Group|null} Left arm pivot group */
  leftArm = null

  /** @type {THREE.Group|null} Right arm pivot group */
  rightArm = null

  /** @type {THREE.Group|null} Left leg pivot group */
  leftLeg = null

  /** @type {THREE.Group|null} Right leg pivot group */
  rightLeg = null

  /** @type {number} Animation time accumulator */
  animTime = 0

  /** @type {THREE.Scene|null} Scene reference for add/remove */
  #scene = null

  /** @type {number} Last position for speed calculation */
  #lastX = 0
  /** @type {number} Last position for speed calculation */
  #lastZ = 0
  /** @type {number} Last time for speed calculation */
  #lastTime = 0
  /** @type {number} Smoothed horizontal speed */
  #smoothedSpeed = 0

  /**
   * @param {GlobalEntityId} globalId  GlobalEntityId.
   * @param {EntityType} entityType  EntityType (PLAYER or GHOST_PLAYER).
   * @param {THREE.Scene} scene  Three.js scene to add mesh to.
   * @param {boolean} isSelf  True if this is the local player entity.
   */
  constructor(globalId, entityType, scene, isSelf = false) {
    super(globalId, entityType)
    this.#scene = scene
    this.isSelf = isSelf
    if (!isSelf) {
      this.mesh = this.#createMesh(entityType)
      scene.add(this.mesh)
    }
    this.#lastX = this.currentPos.x
    this.#lastZ = this.currentPos.z
    this.#lastTime = performance.now()
  }

  /**
   * Mark this entity as the self player. If a mesh exists, remove it.
   */
  markAsSelf() {
    if (this.isSelf) return
    this.isSelf = true
    if (this.mesh && this.#scene) {
      this.#destroyMesh()
    }
  }

  /**
   * Create the player mesh hierarchy.
   * Total height with SIZE=1.0: ~1.6 voxels (head top at y=1.2, feet at y=-0.4)
   * @param {EntityType} entityType
   * @returns {THREE.Group}
   */
  #createMesh(entityType) {
    const group = new THREE.Group()

    // Scale factor for all mesh parts (1.0 = base size)
    const SIZE = .7
    // Vertical offset to adjust mesh position relative to entity center
    const HEIGHT_OFFSET = -.5

    // Color based on entity type
    const isGhost = entityType === EntityType.GHOST_PLAYER
    const shirtColor = isGhost ? 0x88ccff : 0x00aaaa  // Light blue for ghost, cyan for player
    const skinColor = isGhost ? 0xffddaa : 0xffccaa   // Skin tone
    const pantsColor = isGhost ? 0x446688 : 0x3333aa  // Darker pants

    const shirtMat = new THREE.MeshLambertMaterial({ color: shirtColor })
    const skinMat = new THREE.MeshLambertMaterial({ color: skinColor })
    const pantsMat = new THREE.MeshLambertMaterial({ color: pantsColor })
    const eyeMat = new THREE.MeshBasicMaterial({ color: 0x333333 })

    // --- Head ---
    // Base: 0.4 x 0.4 x 0.4, centered at y=1.0
    const headSize = 0.4 * SIZE
    const headGeo = new THREE.BoxGeometry(headSize, headSize, headSize)
    const head = new THREE.Mesh(headGeo, skinMat)
    head.position.y = 1.0 * SIZE + HEIGHT_OFFSET
    head.castShadow = true
    head.receiveShadow = true
    group.add(head)

    // Eyes
    const eyeSize = 0.06 * SIZE
    const eyeDepth = 0.02 * SIZE
    const eyeGeo = new THREE.BoxGeometry(eyeSize, eyeSize, eyeDepth)
    const leftEye = new THREE.Mesh(eyeGeo, eyeMat)
    leftEye.position.set(-0.1 * SIZE, 1.05 * SIZE + HEIGHT_OFFSET, 0.21 * SIZE)
    group.add(leftEye)
    const rightEye = new THREE.Mesh(eyeGeo, eyeMat)
    rightEye.position.set(0.1 * SIZE, 1.05 * SIZE + HEIGHT_OFFSET, 0.21 * SIZE)
    group.add(rightEye)

    // --- Body ---
    // Base: 0.4 x 0.6 x 0.2, centered at y=0.5
    const bodyW = 0.4 * SIZE
    const bodyH = 0.6 * SIZE
    const bodyD = 0.2 * SIZE
    const bodyGeo = new THREE.BoxGeometry(bodyW, bodyH, bodyD)
    const body = new THREE.Mesh(bodyGeo, shirtMat)
    body.position.y = 0.5 * SIZE + HEIGHT_OFFSET
    body.castShadow = true
    body.receiveShadow = true
    group.add(body)

    // --- Arms ---
    // Base: 0.15 x 0.6 x 0.15 each
    // Pivot at shoulder (y=0.8), extends down to y=0.2
    const armW = 0.15 * SIZE
    const armH = 0.6 * SIZE
    const armD = 0.15 * SIZE
    const armGeo = new THREE.BoxGeometry(armW, armH, armD)

    // Left arm
    const leftArmGroup = new THREE.Group()
    leftArmGroup.position.set(-0.275 * SIZE, 0.8 * SIZE + HEIGHT_OFFSET, 0)
    const leftArmMesh = new THREE.Mesh(armGeo, shirtMat)
    leftArmMesh.position.y = -0.3 * SIZE // Offset so pivot is at top
    leftArmMesh.castShadow = true
    leftArmMesh.receiveShadow = true
    leftArmGroup.add(leftArmMesh)
    group.add(leftArmGroup)
    this.leftArm = leftArmGroup

    // Right arm
    const rightArmGroup = new THREE.Group()
    rightArmGroup.position.set(0.275 * SIZE, 0.8 * SIZE + HEIGHT_OFFSET, 0)
    const rightArmMesh = new THREE.Mesh(armGeo, shirtMat)
    rightArmMesh.position.y = -0.3 * SIZE // Offset so pivot is at top
    rightArmMesh.castShadow = true
    rightArmMesh.receiveShadow = true
    rightArmGroup.add(rightArmMesh)
    group.add(rightArmGroup)
    this.rightArm = rightArmGroup

    // --- Legs ---
    // Base: 0.18 x 0.6 x 0.18 each
    // Pivot at hip (y=0.2), extends down to y=-0.4 (feet)
    const legW = 0.18 * SIZE
    const legH = 0.6 * SIZE
    const legD = 0.18 * SIZE
    const legGeo = new THREE.BoxGeometry(legW, legH, legD)

    // Left leg
    const leftLegGroup = new THREE.Group()
    leftLegGroup.position.set(-0.1 * SIZE, 0.2 * SIZE + HEIGHT_OFFSET, 0)
    const leftLegMesh = new THREE.Mesh(legGeo, pantsMat)
    leftLegMesh.position.y = -0.3 * SIZE // Offset so pivot is at top
    leftLegMesh.castShadow = true
    leftLegMesh.receiveShadow = true
    leftLegGroup.add(leftLegMesh)
    group.add(leftLegGroup)
    this.leftLeg = leftLegGroup

    // Right leg
    const rightLegGroup = new THREE.Group()
    rightLegGroup.position.set(0.1 * SIZE, 0.2 * SIZE + HEIGHT_OFFSET, 0)
    const rightLegMesh = new THREE.Mesh(legGeo, pantsMat)
    rightLegMesh.position.y = -0.3 * SIZE // Offset so pivot is at top
    rightLegMesh.castShadow = true
    rightLegMesh.receiveShadow = true
    rightLegGroup.add(rightLegMesh)
    group.add(rightLegGroup)
    this.rightLeg = rightLegGroup

    return group
  }

  /**
   * Update animation and position.
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

    // Calculate speed from actual position change (sub-voxels per second)
    const now = performance.now()
    const timeDelta = (now - this.#lastTime) / 1000

    if (timeDelta > 0) {
      const dx = pos.x - this.#lastX
      const dz = pos.z - this.#lastZ
      const instantSpeed = Math.sqrt(dx * dx + dz * dz) / timeDelta // sub-voxels per second

      // Smooth the speed for more stable animation
      this.#smoothedSpeed = this.#smoothedSpeed * 0.8 + instantSpeed * 0.2

      // Update facing direction based on movement (horizontal rotation only)
      const moveThreshold = 50 // sub-voxels/second threshold to change facing
      if (this.#smoothedSpeed > moveThreshold && (Math.abs(dx) > 0 || Math.abs(dz) > 0)) {
        // atan2(x, z) gives yaw angle where +Z is forward, +X is right
        const targetYaw = Math.atan2(dx, dz)
        // Smooth rotation
        let yawDiff = targetYaw - this.mesh.rotation.y
        while (yawDiff > Math.PI) yawDiff -= Math.PI * 2
        while (yawDiff < -Math.PI) yawDiff += Math.PI * 2
        this.mesh.rotation.y += yawDiff * 0.15 // Smooth turn speed
      }

      this.#lastX = pos.x
      this.#lastZ = pos.z
      this.#lastTime = now
    }

    // Animate arms and legs based on speed
    // Walking threshold: ~200 sub-voxels/second (~0.8 voxels/second)
    const WALK_SPEED_THRESHOLD = 200

    if (this.#smoothedSpeed > WALK_SPEED_THRESHOLD) {
      this.animTime += dt * 10 // Animation speed multiplier
      const swing = Math.sin(this.animTime)

      // Diagonal pairs swing together (opposite phase between arms and legs)
      if (this.leftArm) this.leftArm.rotation.x = swing * 0.8
      if (this.rightArm) this.rightArm.rotation.x = -swing * 0.8
      if (this.leftLeg) this.leftLeg.rotation.x = -swing * 0.8
      if (this.rightLeg) this.rightLeg.rotation.x = swing * 0.8
    } else {
      // Return to neutral pose when idle
      this.animTime = 0
      const returnSpeed = 0.2

      if (this.leftArm) this.leftArm.rotation.x *= (1 - returnSpeed)
      if (this.rightArm) this.rightArm.rotation.x *= (1 - returnSpeed)
      if (this.leftLeg) this.leftLeg.rotation.x *= (1 - returnSpeed)
      if (this.rightLeg) this.rightLeg.rotation.x *= (1 - returnSpeed)
    }
  }

  /**
   * Remove mesh from scene and dispose resources.
   * @private
   */
  #destroyMesh() {
    if (!this.mesh || !this.#scene) return

    this.#scene.remove(this.mesh)
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

  /**
   * Remove mesh from scene.
   * @param {THREE.Scene} scene
   */
  destroy(scene) {
    this.#destroyMesh()
    this.#scene = null
  }

  // ─── Static Deserialization Methods ───────────────────────────────────────

  /**
   * Deserialize player entity creation into an existing entity.
   * 
   * For CREATE_ENTITY: Reset ALL components to defaults first, then deserialize
   * only the components indicated by componentMask. Missing components remain
   * at their default values.
   * 
   * @param {PlayerEntity} entity - The entity to deserialize into (already created)
   * @param {BufReader} reader
   * @param {number} componentMask - Bitmask indicating which components are present
   * @param {number} messageTick
   */
  static deserializeCreate(entity, reader, componentMask, messageTick) {
    if (entity) {
      // 1. Reset ALL components to defaults first
      entity.motion.resetToDefaults()
    }

    // 2. Deserialize only components indicated by mask (missing = stay at default)
    // If entity is null, component data is still read from reader but discarded
    PlayerEntity.deserializeComponents(entity, reader, componentMask, messageTick)
  }

  /**
   * Deserialize ghost player entity creation into an existing entity.
   * Same as player but for GHOST_PLAYER type.
   * 
   * @param {PlayerEntity} entity - The entity to deserialize into (already created)
   * @param {BufReader} reader
   * @param {number} componentMask - Bitmask indicating which components are present
   * @param {number} messageTick
   */
  static deserializeCreateGhost(entity, reader, componentMask, messageTick) {
    if (entity) {
      // 1. Reset ALL components to defaults first
      entity.motion.resetToDefaults()
    }

    // 2. Deserialize only components indicated by mask (missing = stay at default)
    // If entity is null, component data is still read from reader but discarded
    PlayerEntity.deserializeComponents(entity, reader, componentMask, messageTick)
  }

  /**
   * Deserialize player entity update.
   * @param {PlayerEntity?} entity
   * @param {BufReader} reader
   * @param {number} componentMask
   * @param {number} messageTick
   */
  static deserializeUpdate(entity, reader, componentMask, messageTick) {
    PlayerEntity.deserializeComponents(entity, reader, componentMask, messageTick)
  }

  /**
   * @param {PlayerEntity?} self 
   * @param {BufReader} reader 
   * @param {number} componentMask 
   * @param {number} messageTick 
   */
  static deserializeComponents(self, reader, componentMask, messageTick) {
    if (componentMask & POSITION_BIT) DynamicPositionComponent.deserialize(self?.motion, reader, messageTick)
  }
}
