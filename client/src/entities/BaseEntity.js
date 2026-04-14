// @ts-check
import { DynamicPositionComponent } from '../components/DynamicPositionComponent.js'

/** @typedef {import('../types.js').ChunkId} ChunkId */
/** @typedef {import('../types.js').SubVoxelCoord} SubVoxelCoord */
/** @typedef {import('../types.js').GlobalEntityId} GlobalEntityId */
/** @typedef {import('../types.js').EntityType} EntityType */

/**
 * @class BaseEntity
 * @abstract
 * @description Client-side base entity — holds components, drives deserialization.
 * Does not render. Mirrors server BaseEntity.
 *
 * Position access:
 * - Use `currentPos` getter for render position (predicted, updated each tick)
 * - Use `receivedPos` getter for last confirmed server position
 *
 * Snapshot record format (server → client):
 *   uint32 GlobalEntityId · uint8 EntityType · uint8 ComponentFlags · ComponentStates…
 *
 * Delta record format:
 *   uint8 ComponentFlags · ComponentStates…   (DeltaType/EntityId/EntityType
 *   are consumed by the outer loop before applyDelta() is called)
 */
export class BaseEntity {
  /** @type {GlobalEntityId} GlobalEntityId (uint32) */
  id

  /** @type {EntityType} EntityType (uint8) */
  type

  /** @type {ChunkId|undefined} Current chunk ID (updated when entity moves) */
  chunkId

  /** @type {number} Last tick when entity received a CREATE state */
  lastCreateTick = 0

  motion = new DynamicPositionComponent()

  /**
   * @param {GlobalEntityId} id    GlobalEntityId.
   * @param {EntityType} type  EntityType.
   */
  constructor(id, type) {
    this.id   = id
    this.type = type
  }

  /**
   * Mark entity as created at given tick.
   * Called by deserializers when CREATE state is received.
   * @param {number} tick
   */
  markCreated(tick) {
    this.lastCreateTick = tick
  }

  /**
   * Get current predicted world position (sub-voxel coordinates).
   * Updated by PhysicsPredictionSystem each tick. Use this for rendering.
   * Divide by SUBVOXEL_SIZE for Three.js world-space coordinates.
   * @returns {{x: SubVoxelCoord, y: SubVoxelCoord, z: SubVoxelCoord}}
   */
  get currentPos() {
    return this.motion.getCurrentPos()
  }

  /**
   * Get last confirmed server position (sub-voxel coordinates).
   * Use this when you need the authoritative server state.
   * Divide by SUBVOXEL_SIZE for Three.js world-space coordinates.
   * @returns {{x: SubVoxelCoord, y: SubVoxelCoord, z: SubVoxelCoord}}
   */
  get receivedPos() {
    return this.motion.getReceivedPos()
  }

  /**
   * Get bounding box half-extents in sub-voxels.
   * Override in subclasses to return correct dimensions.
   * @returns {{hx: number, hy: number, hz: number}} Half-extents in sub-voxels
   */
  getBoundingBox() {
    // Default: no bounding box
    return { hx: 0, hy: 0, hz: 0 }
  }

}
