// @ts-check
import { GRAVITY_DECREMENT } from '../types.js'

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class DynamicPositionComponent
 * @description 3-D position + velocity + physics flags in one component.
 * Mirrors server DynamicPositionComponent.
 *
 * Both sides use the same integer kinematics so the server only sends an
 * update when the prediction model changes (landing, jumping, player input):
 *
 *   n ticks elapsed = currentTick − tick
 *   x' = x + vx·n
 *   y' = y + vy·n − (grounded ? 0 : GRAVITY_DECREMENT·n·(n+1)/2)
 *   z' = z + vz·n
 *   Divide by SUBVOXEL_SIZE for Three.js world-space coordinates.
 *
 * Serialised layout (component data only):
 *   int32 x,y,z (sub-voxels, advanced to message tick) · int32 vx,vy,vz · uint8 grounded
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
   * Deserialize from reader: x,y,z(i32) · vx,vy,vz(i32) · grounded(u8).
   * Tick is NOT read from the stream — it is passed in from the message header.
   * @param {BufReader} reader
   * @param {number}    messageTick  Server tick from the chunk message header.
   */
  deserialize(reader, messageTick) {
    this.tick     = messageTick
    this.x        = reader.readInt32()
    this.y        = reader.readInt32()
    this.z        = reader.readInt32()
    this.vx       = reader.readInt32()
    this.vy       = reader.readInt32()
    this.vz       = reader.readInt32()
    this.grounded = reader.readUint8() !== 0
  }

  /**
   * Compute predicted position at currentTick without mutating state.
   * Returns sub-voxel coordinates — divide by SUBVOXEL_SIZE for Three.js.
   * @param {number} currentTick
   * @returns {{x: number, y: number, z: number}}
   */
  predictAt(currentTick) {
    const n = currentTick - this.tick
    if (n <= 0) return { x: this.x, y: this.y, z: this.z }
    const gravY = this.grounded ? 0 : GRAVITY_DECREMENT * n * (n + 1) / 2
    return {
      x: this.x + this.vx * n,
      y: this.y + this.vy * n - gravY,
      z: this.z + this.vz * n,
    }
  }
}
