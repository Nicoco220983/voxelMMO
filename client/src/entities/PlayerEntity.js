// @ts-check
import { BaseEntity } from './BaseEntity.js'
import { EntityType } from '../types.js'

/** @typedef {import('../utils.js').BufReader} BufReader */

/**
 * @class PlayerEntity
 * @abstract
 * @extends BaseEntity
 * @description Client-side player entity. Mirrors server PlayerEntity.
 * Does not render. Concrete subclasses should add a Three.js representation.
 */
export class PlayerEntity extends BaseEntity {
  /** @type {number} PlayerId (uint32) */
  playerId

  /**
   * @param {number} id        EntityId.
   * @param {number} playerId  PlayerId.
   */
  constructor(id, playerId) {
    super(id, EntityType.PLAYER)
    this.playerId = playerId
  }
}
