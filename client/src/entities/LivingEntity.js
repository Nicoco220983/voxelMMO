// @ts-check
import { BaseEntity } from './BaseEntity.js'
import { HealthComponent } from '../components/HealthComponent.js'

/** @typedef {import('../types.js').GlobalEntityId} GlobalEntityId */
/** @typedef {import('../types.js').EntityType} EntityType */

/**
 * Flash duration in seconds when entity takes damage.
 * @type {number}
 */
const DAMAGE_FLASH_DURATION = 0.3

/**
 * Red color for damage flash (RGB hex).
 * @type {number}
 */
const DAMAGE_FLASH_COLOR = 0xff3333

/**
 * Speed of death tilt in radians per second.
 * @type {number}
 */
const DEATH_TILT_SPEED = Math.PI * 2

/**
 * @class LivingEntity
 * @extends BaseEntity
 * @abstract
 * @description Base class for living entities with health and damage visualization.
 *
 * Provides damage flash effect: when the entity takes damage, its mesh briefly
turns red to provide visual feedback to the player.
 *
 * Subclasses must have a `mesh` property (THREE.Group or THREE.Mesh).
 */
export class LivingEntity extends BaseEntity {
  /** @type {HealthComponent} Health component for damage tracking */
  health = new HealthComponent()

  /** @type {number} Remaining flash time in seconds */
  #damageFlashTime = 0

  /** @type {Map<import('three').Mesh, import('three').Material>} Saved original materials */
  #originalMaterials = new Map()

  /** @type {boolean} Whether currently in flash state */
  #isFlashing = false

  /** @type {boolean} Whether entity was dead on last check */
  #wasDead = false

  /** @type {number} Current Z-axis death tilt in radians */
  #deathRotation = 0

  /**
   * @param {GlobalEntityId} id    GlobalEntityId.
   * @param {EntityType} type  EntityType.
   */
  constructor(id, type) {
    super(id, type)
  }

  /**
   * Check if entity was damaged and start flash effect if so.
   * Call this from updateAnimation() each frame.
   */
  checkDamage() {
    if (this.health.hasBeenDamaged()) {
      this.#startDamageFlash()
    }
  }

  /**
   * Update damage flash timing. Call this from updateAnimation() each frame.
   * @param {number} dt  Delta time in seconds.
   */
  updateDamageFlash(dt) {
    if (!this.#isFlashing) return

    this.#damageFlashTime -= dt
    if (this.#damageFlashTime <= 0) {
      this.#restoreMaterials()
    }
  }

  /**
   * Start the damage flash effect.
   * Saves original materials and sets all mesh materials to red.
   * @private
   */
  #startDamageFlash() {
    // @ts-ignore - subclasses have mesh property
    if (!this.mesh) return

    // Reset any existing flash first
    this.#restoreMaterials()

    this.#damageFlashTime = DAMAGE_FLASH_DURATION
    this.#isFlashing = true

    // Collect all meshes and save original materials
    // @ts-ignore
    this.mesh.traverse((child) => {
      if (child.isMesh && child.material) {
        // Save original material
        this.#originalMaterials.set(child, child.material)

        // Clone and tint red
        const redMaterial = child.material.clone()
        redMaterial.color.setHex(DAMAGE_FLASH_COLOR)
        if (redMaterial.emissive) {
          redMaterial.emissive.setHex(0x550000)
        }
        child.material = redMaterial
      }
    })
  }

  /**
   * Restore original materials after flash ends.
   * @private
   */
  #restoreMaterials() {
    if (!this.#isFlashing) return

    // Restore original materials
    for (const [mesh, originalMaterial] of this.#originalMaterials) {
      if (mesh.material) {
        mesh.material.dispose()
      }
      mesh.material = originalMaterial
    }

    this.#originalMaterials.clear()
    this.#isFlashing = false
    this.#damageFlashTime = 0
  }

  /**
   * Check if entity is dead.
   * @returns {boolean}
   */
  get isDead() {
    return this.health.isDead
  }

  /**
   * Update death rotation (fall over effect). Call from updateAnimation() each frame.
   * @param {number} dt  Delta time in seconds.
   */
  updateDeathRotation(dt) {
    // @ts-ignore - subclasses have mesh property
    if (!this.mesh) return

    const dead = this.health.isDead
    if (!dead) {
      if (this.#deathRotation !== 0) {
        this.#deathRotation = 0
        // @ts-ignore
        this.mesh.rotation.z = 0
      }
      this.#wasDead = false
      return
    }

    const target = Math.PI / 2
    const step = DEATH_TILT_SPEED * dt
    if (this.#deathRotation < target) {
      this.#deathRotation = Math.min(target, this.#deathRotation + step)
    }
    // @ts-ignore
    this.mesh.rotation.z = this.#deathRotation
    this.#wasDead = true
  }

  /**
   * Clean up resources. Call from subclass destroy() method.
   * @protected
   */
  _cleanupDamageFlash() {
    this.#restoreMaterials()
  }
}
