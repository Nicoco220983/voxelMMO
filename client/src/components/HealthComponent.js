// @ts-check
import { BaseComponent } from './BaseComponent.js'

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class HealthComponent
 * @extends BaseComponent
 * @description Health state for damageable entities. Mirrors server HealthComponent.
 *
 * Tracks current/max health and the tick when last damage was taken.
 *
 * Serialized layout (component data only):
 *   uint16 current | uint16 max | uint32 lastDamageTick
 */
export class HealthComponent extends BaseComponent {
  /** @type {Readonly<HealthComponent>} Default values for CREATE deserialization */
  static DEFAULT = Object.freeze({
    current: 0,
    max: 0,
    lastDamageTick: 0
  })

  /** @type {number} Current health points */
  current = 0

  /** @type {number} Maximum health points */
  max = 0

  /** @type {number} Tick when health last changed (from server) */
  lastDamageTick = 0

  /** @type {number} Client tick when damage was last received (0 = no damage yet) */
  damageReceivedTick = 0

  /**
   * Reset all fields to default values.
   * Called during CREATE_ENTITY deserialization before selective component reading.
   */
  resetToDefaults() {
    this.current = HealthComponent.DEFAULT.current
    this.max = HealthComponent.DEFAULT.max
    this.lastDamageTick = HealthComponent.DEFAULT.lastDamageTick
  }

  /**
   * Check if entity is dead (health is 0).
   * @returns {boolean}
   */
  get isDead() {
    return this.current === 0 && this.max > 0
  }

  /**
   * Get health percentage (0.0 to 1.0).
   * @returns {number}
   */
  get healthPercent() {
    if (this.max === 0) return 0
    return this.current / this.max
  }

  /**
   * Check if entity was damaged since last check.
   * Call this from the render loop to detect damage for visual feedback.
   * Returns true only once per damage event (resets after check).
   * @returns {boolean}
   */
  hasBeenDamaged() {
    if (this.damageReceivedTick === 0) return false
    // Reset after reading so it only triggers once
    const wasDamaged = true
    this.damageReceivedTick = 0
    return wasDamaged
  }

  /**
   * Deserialize from reader: current(u16) | max(u16) | lastDamageTick(u32).
   * Sets damageReceivedTick if health decreased.
   * @param {HealthComponent?} self
   * @param {BufReader} reader
   * @param {number} messageTick Server tick from the chunk message header.
   */
  static deserialize(self, reader, messageTick) {
    const current = reader.readUint16()
    const max = reader.readUint16()
    const lastDamageTick = reader.readUint32()

    if (!self) {
      console.debug('[HealthComponent] discarded')
      return
    }

    // Check for damage before updating
    const healthDecreased = current < self.current

    self.current = current
    self.max = max
    self.lastDamageTick = lastDamageTick
    self.markUpdated(messageTick)

    // Track damage received tick for visual feedback
    if (healthDecreased) {
      self.damageReceivedTick = messageTick
    }

    console.debug('[HealthComponent] deserialized:', {
      current: self.current,
      max: self.max,
      lastDamageTick: self.lastDamageTick,
      isDead: self.isDead,
      healthDecreased
    })
  }
}
