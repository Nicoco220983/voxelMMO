// @ts-check
import { DynamicPositionComponent } from '../components/DynamicPositionComponent.js'
import { POSITION_BIT } from '../types.js'

/** @typedef {import('../utils.js').BufReader} BufReader */
/** @typedef {import('../types.js').ChunkIdPacked} ChunkIdPacked */

/**
 * @class BaseEntity
 * @abstract
 * @description Client-side base entity — holds components, drives deserialization
 * and straight-line prediction. Does not render. Mirrors server BaseEntity.
 *
 * Snapshot record format (server → client):
 *   uint32 GlobalEntityId · uint8 EntityType · uint8 ComponentFlags · ComponentStates…
 *
 * Delta record format:
 *   uint8 ComponentFlags · ComponentStates…   (DeltaType/EntityId/EntityType
 *   are consumed by the outer loop before applyDelta() is called)
 */
export class BaseEntity {
  /** @type {number} GlobalEntityId (uint32) */
  id

  /** @type {number} EntityType (uint8) */
  type

  /** @type {ChunkIdPacked|undefined} Current chunk ID (updated when entity moves) */
  chunkId

  motion = new DynamicPositionComponent()

  /**
   * @param {number} id    GlobalEntityId.
   * @param {number} type  EntityType.
   */
  constructor(id, type) {
    this.id   = id
    this.type = type
  }

  /**
   * Deserialize the components flagged by the bitmask from the reader.
   * @param {BufReader} reader
   * @param {number}    flags        ComponentFlags byte.
   * @param {number}    messageTick  Server tick from the chunk message header.
   */
  applyComponents(reader, flags, messageTick) {
    if (flags & POSITION_BIT) this.motion.deserialize(reader, messageTick)
  }

  /**
   * Read ComponentFlags from the reader then deserialize the flagged components.
   * Call this when the reader is positioned at the ComponentFlags byte of a
   * delta record.
   * @param {BufReader} reader
   * @param {number}    messageTick  Server tick from the chunk message header.
   */
  applyDelta(reader, messageTick) {
    this.applyComponents(reader, reader.readUint8(), messageTick)
  }

  /**
   * Compute predicted world position at currentTick without mutating state.
   * @param {number} currentTick
   * @returns {{x: number, y: number, z: number}}
   */
  predictAt(currentTick) {
    return this.motion.predictAt(currentTick)
  }

  /**
   * Read a full snapshot entity record and return a populated BaseEntity.
   * Reads: GlobalEntityId(u32) + EntityType(u8) + ComponentFlags(u8) + ComponentStates…
   * @param {BufReader} reader
   * @param {number}    messageTick  Server tick from the chunk message header.
   * @returns {BaseEntity}
   */
  static fromRecord(reader, messageTick) {
    const id    = reader.readUint32()   // GlobalEntityId (uint32, was uint16)
    const type  = reader.readUint8()
    const flags = reader.readUint8()
    const entity = new BaseEntity(id, type)
    entity.applyComponents(reader, flags, messageTick)
    return entity
  }
}
