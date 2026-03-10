// @ts-check
import { GRAVITY_DECREMENT } from '../types.js'

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class DynamicPositionComponent
 * @description 3-D position + velocity + physics flags. Mirrors server.
 * 
 * Stores TWO sets of state:
 * - received*: Last confirmed state from server (immutable between server updates)
 * - current*: Predicted position for current render frame (updated each tick)
 *
 * Both sides use the same integer kinematics so the server only sends an
 * update when the prediction model changes (landing, jumping, player input):
 *
 *   n ticks elapsed = currentTick − receivedTick
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
  // ── Received state (from server) ──────────────────────────────────────────
  /** @type {number} Reference tick when this state was received from server. */
  receivedTick = 0
  /** @type {number} */ receivedX  = 0
  /** @type {number} */ receivedY  = 0
  /** @type {number} */ receivedZ  = 0
  /** @type {number} */ receivedVx = 0
  /** @type {number} */ receivedVy = 0
  /** @type {number} */ receivedVz = 0
  /** @type {boolean} When false, gravity applies. */
  receivedGrounded = false

  // ── Current predicted state (computed each tick) ──────────────────────────
  /** @type {number} */ currentX = 0
  /** @type {number} */ currentY = 0
  /** @type {number} */ currentZ = 0

  /**
   * Deserialize from reader: x,y,z(i32) · vx,vy,vz(i32) · grounded(u8).
   * Updates the received* fields. Does NOT update current* - prediction
   * happens separately via updatePrediction().
   * @param {BufReader} reader
   * @param {number}    messageTick  Server tick from the chunk message header.
   */
  deserialize(reader, messageTick) {
    this.receivedTick     = messageTick
    this.receivedX        = reader.readInt32()
    this.receivedY        = reader.readInt32()
    this.receivedZ        = reader.readInt32()
    this.receivedVx       = reader.readInt32()
    this.receivedVy       = reader.readInt32()
    this.receivedVz       = reader.readInt32()
    this.receivedGrounded = reader.readUint8() !== 0
    
    // Initialize current position to received position
    // Prediction will update it on next tick
    this.currentX = this.receivedX
    this.currentY = this.receivedY
    this.currentZ = this.receivedZ
    
    console.debug('[DynamicPositionComponent] deserialized:', {
      tick: this.receivedTick,
      x: this.receivedX, y: this.receivedY, z: this.receivedZ,
      vx: this.receivedVx, vy: this.receivedVy, vz: this.receivedVz,
      grounded: this.receivedGrounded
    })
  }

  /**
   * Update current* position based on received state and elapsed ticks.
   * Called once per tick by PhysicsPredictionSystem.
   * @param {number} currentTick
   */
  updatePrediction(currentTick) {
    const { receivedTick, receivedX, receivedY, receivedZ, 
            receivedVx, receivedVy, receivedVz, receivedGrounded } = this
    
    // No velocity and grounded = no prediction needed
    if (receivedVx === 0 && receivedVy === 0 && receivedVz === 0 && receivedGrounded) {
      this.currentX = receivedX
      this.currentY = receivedY
      this.currentZ = receivedZ
      return
    }
    
    const n = currentTick - receivedTick
    if (n < 0) {
      console.warn(`DynamicPositionComponent receivedTick (${receivedTick}) newer than global tick (${currentTick})`)
      this.currentX = receivedX
      this.currentY = receivedY
      this.currentZ = receivedZ
      return
    }
    
    const gravY = receivedGrounded ? 0 : GRAVITY_DECREMENT * n * (n + 1) / 2
    
    this.currentX = receivedX + receivedVx * n
    this.currentY = receivedY + receivedVy * n - gravY
    this.currentZ = receivedZ + receivedVz * n
  }

  /**
   * Get current predicted position as of last updatePrediction() call.
   * Returns sub-voxel coordinates — divide by SUBVOXEL_SIZE for Three.js.
   * @returns {{x: number, y: number, z: number}}
   */
  getCurrentPos() {
    return {
      x: this.currentX,
      y: this.currentY,
      z: this.currentZ,
    }
  }

  /**
   * Get received position (last confirmed server state).
   * Returns sub-voxel coordinates.
   * @returns {{x: number, y: number, z: number}}
   */
  getReceivedPos() {
    return {
      x: this.receivedX,
      y: this.receivedY,
      z: this.receivedZ,
    }
  }
}
