// @ts-check
import { Chunk } from './Chunk.js'

/** @typedef {import('./types.js').ChunkIdPacked} ChunkIdPacked */

/**
 * @class ChunkRegistry
 * @description Manages all chunk instances and their entity memberships.
 * Each chunk owns a set of GlobalEntityIds representing entities currently in that chunk.
 * This is separate from EntityRegistry which manages the actual entity objects.
 */
export class ChunkRegistry {
  /** @type {Map<ChunkIdPacked, Chunk>} */
  #chunks = new Map()

  /**
   * Get a chunk by its ID.
   * @param {ChunkIdPacked} chunkId
   * @returns {Chunk|undefined}
   */
  get(chunkId) {
    return this.#chunks.get(chunkId)
  }

  /**
   * Get or create a chunk.
   * @param {ChunkIdPacked} chunkId
   * @returns {Chunk}
   */
  getOrCreate(chunkId) {
    let chunk = this.#chunks.get(chunkId)
    if (!chunk) {
      chunk = new Chunk(chunkId)
      this.#chunks.set(chunkId, chunk)
    }
    return chunk
  }

  /**
   * Check if a chunk exists.
   * @param {ChunkIdPacked} chunkId
   * @returns {boolean}
   */
  has(chunkId) {
    return this.#chunks.has(chunkId)
  }

  /**
   * Remove a chunk and dispose its resources.
   * @param {ChunkIdPacked} chunkId
   * @param {THREE.Scene} scene
   * @returns {boolean} True if chunk was removed
   */
  remove(chunkId, scene) {
    const chunk = this.#chunks.get(chunkId)
    if (!chunk) return false
    chunk.dispose(scene)
    this.#chunks.delete(chunkId)
    return true
  }

  /**
   * Get all chunk IDs.
   * @returns {IterableIterator<ChunkIdPacked>}
   */
  keys() {
    return this.#chunks.keys()
  }

  /**
   * Get all chunks.
   * @returns {IterableIterator<Chunk>}
   */
  values() {
    return this.#chunks.values()
  }

  /**
   * Get all chunk entries.
   * @returns {IterableIterator<[ChunkIdPacked, Chunk]>}
   */
  entries() {
    return this.#chunks.entries()
  }

  /**
   * Clear all chunks and dispose their resources.
   * @param {THREE.Scene} scene
   */
  clear(scene) {
    for (const chunk of this.#chunks.values()) {
      chunk.dispose(scene)
    }
    this.#chunks.clear()
  }

  /**
   * Get the number of chunks.
   * @returns {number}
   */
  get size() {
    return this.#chunks.size
  }
}
