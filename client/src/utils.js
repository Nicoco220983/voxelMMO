// @ts-check
import LZ4 from 'lz4js'

/**
 * @class BufReader
 * @description Sequential binary reader over a DataView — mirrors the server's BufWriter.
 */
export class BufReader {
  /** @type {DataView} */ #view
  /** @type {number}   */ offset

  /**
   * @param {DataView} view
   * @param {number}   [offset=0]  Initial read position.
   */
  constructor(view, offset = 0) {
    this.#view  = view
    this.offset = offset
  }

  /** @returns {number} */
  readUint8()   { return this.#view.getUint8(this.offset++) }

  /** @returns {number} */
  readUint16()  { const v = this.#view.getUint16(this.offset, true); this.offset += 2; return v }

  /** @returns {number} */
  readInt32()   { const v = this.#view.getInt32(this.offset, true);  this.offset += 4; return v }

  /** @returns {number} */
  readUint32()  { const v = this.#view.getUint32(this.offset, true);  this.offset += 4; return v }

  /** @returns {number} */
  readFloat32() { const v = this.#view.getFloat32(this.offset, true); this.offset += 4; return v }

  /** @returns {bigint} */
  readInt64()   { 
    const lo = this.#view.getUint32(this.offset, true)
    const hi = this.#view.getInt32(this.offset + 4, true)
    this.offset += 8
    return (BigInt(hi) << 32n) | BigInt(lo)
  }
}

/**
 * Decompress a raw LZ4 block into a new Uint8Array.
 * @param {Uint8Array} src              Compressed bytes.
 * @param {number}     uncompressedSize Expected output size in bytes.
 * @returns {Uint8Array}
 */
export function lz4Decompress(src, uncompressedSize) {
  const dst = new Uint8Array(uncompressedSize)
  LZ4.decompressBlock(src, dst, 0, src.length, 0)
  return dst
}
