// @ts-check
import { DynamicPositionComponent } from '../components/DynamicPositionComponent.js'
import { GameContext } from '../GameContext.js'
import { SUBVOXEL_SIZE } from '../types.js'
import * as THREE from 'three'

/** @typedef {import('../types.js').ChunkId} ChunkId */
/** @typedef {import('../types.js').SubVoxelCoord} SubVoxelCoord */
/** @typedef {import('../types.js').GlobalEntityId} GlobalEntityId */
/** @typedef {import('../types.js').EntityType} EntityType */

// Shared debug bounding box resources
const _debugBoxGeometry = new THREE.BoxGeometry(1, 1, 1)
const _debugBoxMaterial = new THREE.LineBasicMaterial({
  color: 0x00ff00,
  transparent: true,
  opacity: 0.8,
  depthTest: false,
})

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

  /** @type {THREE.LineSegments|null} */
  #debugWireframe = null

  /**
   * Update debug visualization for this entity.
   * Creates, updates, or removes the bounding box wireframe based on GameContext.debugMode.
   * @param {THREE.Scene} scene
   */
  updateDebug(scene) {
    if (!GameContext.debugMode) {
      if (this.#debugWireframe) {
        scene.remove(this.#debugWireframe)
        this.#debugWireframe.geometry.dispose()
        this.#debugWireframe = null
      }
      return
    }

    const bbox = this.getBoundingBox()
    if (bbox.hx === 0 && bbox.hy === 0 && bbox.hz === 0) {
      if (this.#debugWireframe) {
        scene.remove(this.#debugWireframe)
        this.#debugWireframe.geometry.dispose()
        this.#debugWireframe = null
      }
      return
    }

    if (!this.#debugWireframe) {
      const edges = new THREE.EdgesGeometry(_debugBoxGeometry)
      this.#debugWireframe = new THREE.LineSegments(edges, _debugBoxMaterial)
      scene.add(this.#debugWireframe)
    }

    const pos = this.currentPos
    const centerX = pos.x / SUBVOXEL_SIZE
    const centerY = pos.y / SUBVOXEL_SIZE
    const centerZ = pos.z / SUBVOXEL_SIZE

    const scaleX = (bbox.hx * 2) / SUBVOXEL_SIZE
    const scaleY = (bbox.hy * 2) / SUBVOXEL_SIZE
    const scaleZ = (bbox.hz * 2) / SUBVOXEL_SIZE

    this.#debugWireframe.position.set(centerX, centerY, centerZ)
    this.#debugWireframe.scale.set(scaleX, scaleY, scaleZ)
  }

  /**
   * Remove debug visualization from scene.
   * @param {THREE.Scene} scene
   */
  destroyDebug(scene) {
    if (this.#debugWireframe) {
      scene.remove(this.#debugWireframe)
      this.#debugWireframe.geometry.dispose()
      this.#debugWireframe = null
    }
  }

  /**
   * Destroy this entity and its debug visuals.
   * Subclasses that override this MUST call super.destroy(scene).
   * @param {THREE.Scene} scene
   */
  destroy(scene) {
    this.destroyDebug(scene)
  }

}
