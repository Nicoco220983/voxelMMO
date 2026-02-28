// @ts-check
import { TICK_RATE, GRAVITY } from '../types.js'

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class DynamicPositionComponent
 * @description 3-D position + velocity + physics flags in one component.
 * Mirrors server DynamicPositionComponent.
 *
 * Both sides use the same closed-form kinematics so the server only sends an
 * update when the prediction model changes (landing, jumping, player input):
 *
 *   dt  = (currentTick − tick) / TICK_RATE
 *   x'  = x + vx·dt
 *   y'  = y + vy·dt  −  (grounded ? 0 : ½·GRAVITY·dt²)
 *   z'  = z + vz·dt
 *
 * Serialised layout (component data only):
 *   float x,y,z (advanced to message tick) · float vx,vy,vz · uint8 grounded
 * The reference tick is NOT in the component stream — it comes from the
 * chunk message header and is passed in as messageTick.
 */
export class DynamicPositionComponent {
  /** @type {number} Reference tick when this state was captured. */
  tick     = 0
  /** @type {number} */ x  = 0
  /** @type {number} */ y  = 0
  /** @type {number} */ z  = 0
  /** @type {number} */ vx = 0
  /** @type {number} */ vy = 0
  /** @type {number} */ vz = 0
  /** @type {boolean} When false, gravity applies. */
  grounded = false

  /**
   * Deserialize from reader: x,y,z(f32) · vx,vy,vz(f32) · grounded(u8).
   * Tick is NOT read from the stream — it is passed in from the message header.
   * @param {BufReader} reader
   * @param {number}    messageTick  Server tick from the chunk message header.
   */
  deserialize(reader, messageTick) {
    this.tick     = messageTick
    this.x        = reader.readFloat32()
    this.y        = reader.readFloat32()
    this.z        = reader.readFloat32()
    this.vx       = reader.readFloat32()
    this.vy       = reader.readFloat32()
    this.vz       = reader.readFloat32()
    this.grounded = reader.readUint8() !== 0
  }

  /**
   * Compute predicted world position at currentTick without mutating state.
   * @param {number} currentTick
   * @returns {{x: number, y: number, z: number}}
   */
  predictAt(currentTick) {
    const dt = (currentTick - this.tick) / TICK_RATE
    if (dt <= 0) return { x: this.x, y: this.y, z: this.z }
    return {
      x: this.x + this.vx * dt,
      y: this.y + this.vy * dt - (this.grounded ? 0 : 0.5 * GRAVITY * dt * dt),
      z: this.z + this.vz * dt,
    }
  }
}
