// @ts-check
import { GRAVITY_DECREMENT } from '../types.js'
import { BaseComponent } from './BaseComponent.js'

/** @typedef {import('../utils.js').BufReader} BufReader */
/** @typedef {import('../types.js').SubVoxelCoord} SubVoxelCoord */

/**
 * @class DynamicPositionComponent
 * @extends BaseComponent
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
export class DynamicPositionComponent extends BaseComponent {
  /** @type {Readonly<DynamicPositionComponent>} Default values for CREATE deserialization */
  static DEFAULT = Object.freeze({
    receivedTick: 0,
    receivedX: 0, receivedY: 0, receivedZ: 0,
    receivedVx: 0, receivedVy: 0, receivedVz: 0,
    receivedGrounded: false,
    currentX: 0, currentY: 0, currentZ: 0
  })

  // ── Received state (from server) ──────────────────────────────────────────
  /** @type {number} Reference tick when this state was received from server. */
  receivedTick = 0
  /** @type {SubVoxelCoord} */ receivedX  = 0
  /** @type {SubVoxelCoord} */ receivedY  = 0
  /** @type {SubVoxelCoord} */ receivedZ  = 0
  /** @type {SubVoxelCoord} */ receivedVx = 0
  /** @type {SubVoxelCoord} */ receivedVy = 0
  /** @type {SubVoxelCoord} */ receivedVz = 0
  /** @type {boolean} When false, gravity applies. */
  receivedGrounded = false

  // ── Current predicted state (computed each tick) ──────────────────────────
  /** @type {SubVoxelCoord} */ currentX = 0
  /** @type {SubVoxelCoord} */ currentY = 0
  /** @type {SubVoxelCoord} */ currentZ = 0

  /**
   * Reset all fields to default values.
   * Called during CREATE_ENTITY deserialization before selective component reading.
   */
  resetToDefaults() {
    this.receivedTick = DynamicPositionComponent.DEFAULT.receivedTick
    this.receivedX = DynamicPositionComponent.DEFAULT.receivedX
    this.receivedY = DynamicPositionComponent.DEFAULT.receivedY
    this.receivedZ = DynamicPositionComponent.DEFAULT.receivedZ
    this.receivedVx = DynamicPositionComponent.DEFAULT.receivedVx
    this.receivedVy = DynamicPositionComponent.DEFAULT.receivedVy
    this.receivedVz = DynamicPositionComponent.DEFAULT.receivedVz
    this.receivedGrounded = DynamicPositionComponent.DEFAULT.receivedGrounded
    this.currentX = DynamicPositionComponent.DEFAULT.currentX
    this.currentY = DynamicPositionComponent.DEFAULT.currentY
    this.currentZ = DynamicPositionComponent.DEFAULT.currentZ
  }

  /**
   * Deserialize from reader: x,y,z(i32) · vx,vy,vz(i32) · grounded(u8).
   * Updates the received* fields. Does NOT update current* - prediction
   * happens separately via updatePrediction().
   * @param {DynamicPositionComponent?} self
   * @param {BufReader} reader
   * @param {number}    messageTick  Server tick from the chunk message header.
   */
  static deserialize(self, reader, messageTick) {

    const receivedX        = reader.readInt32()
    if(self) self.receivedX = receivedX
    const receivedY        = reader.readInt32()
    if(self) self.receivedY = receivedY
    const receivedZ        = reader.readInt32()
    if(self) self.receivedZ = receivedZ
    const receivedVx       = reader.readInt32()
    if(self) self.receivedVx = receivedVx
    const receivedVy       = reader.readInt32()
    if(self) self.receivedVy = receivedVy
    const receivedVz       = reader.readInt32()
    if(self) self.receivedVz = receivedVz
    const receivedGrounded = reader.readUint8() !== 0
    if(self) self.receivedGrounded = receivedGrounded
    if(!self) {
      console.debug('[DynamicPositionComponent] discarded')
      return
    }

    // Initialize current position to received position
    // Prediction will update it on next tick
    self.receivedTick     = messageTick
    self.currentX = self.receivedX
    self.currentY = self.receivedY
    self.currentZ = self.receivedZ

    self.markUpdated(messageTick)

    console.debug('[DynamicPositionComponent] deserialized:', {
      tick: self.receivedTick,
      x: self.receivedX, y: self.receivedY, z: self.receivedZ,
      vx: self.receivedVx, vy: self.receivedVy, vz: self.receivedVz,
      grounded: self.receivedGrounded
    })
  }

  /**
   * Update current* position based on received state and elapsed ticks.
   * Called once per render frame by PhysicsPredictionSystem.
   * Supports sub-tick precision for smooth interpolation (currentTick can be float).
   * @param {number} currentTick - Can be integer (tick) or float (sub-tick for smooth rendering)
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
    // Allow small negative tolerance for floating point jitter (shouldn't happen with proper sync)
    if (n < -0.1) {
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
   * @returns {{x: SubVoxelCoord, y: SubVoxelCoord, z: SubVoxelCoord}}
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
   * @returns {{x: SubVoxelCoord, y: SubVoxelCoord, z: SubVoxelCoord}}
   */
  getReceivedPos() {
    return {
      x: this.receivedX,
      y: this.receivedY,
      z: this.receivedZ,
    }
  }
}
