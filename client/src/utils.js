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
  readUint8()   {
    if (this.offset + 1 > this.#view.byteLength) {
      throw new Error(`BufReader overrun: need 1 byte at offset ${this.offset}, have ${this.#view.byteLength}`)
    }
    return this.#view.getUint8(this.offset++)
  }

  /** @returns {number} */
  readUint16()  {
    if (this.offset + 2 > this.#view.byteLength) {
      throw new Error(`BufReader overrun: need 2 bytes at offset ${this.offset}, have ${this.#view.byteLength}`)
    }
    const v = this.#view.getUint16(this.offset, true); this.offset += 2; return v
  }

  /** @returns {number} */
  readInt32()   {
    if (this.offset + 4 > this.#view.byteLength) {
      throw new Error(`BufReader overrun: need 4 bytes at offset ${this.offset}, have ${this.#view.byteLength}`)
    }
    const v = this.#view.getInt32(this.offset, true);  this.offset += 4; return v
  }

  /** @returns {number} */
  readUint32()  {
    if (this.offset + 4 > this.#view.byteLength) {
      throw new Error(`BufReader overrun: need 4 bytes at offset ${this.offset}, have ${this.#view.byteLength}`)
    }
    const v = this.#view.getUint32(this.offset, true);  this.offset += 4; return v
  }

  /** @returns {number} */
  readFloat32() {
    if (this.offset + 4 > this.#view.byteLength) {
      throw new Error(`BufReader overrun: need 4 bytes at offset ${this.offset}, have ${this.#view.byteLength}`)
    }
    const v = this.#view.getFloat32(this.offset, true); this.offset += 4; return v
  }

  /** @returns {bigint} */
  readInt64()   {
    if (this.offset + 8 > this.#view.byteLength) {
      throw new Error(`BufReader overrun: need 8 bytes at offset ${this.offset}, have ${this.#view.byteLength}`)
    }
    const lo = this.#view.getUint32(this.offset, true)
    const hi = this.#view.getInt32(this.offset + 4, true)
    this.offset += 8
    return (BigInt(hi) << 32n) | BigInt(lo)
  }

  /**
   * Skip n bytes without reading.
   * @param {number} n Number of bytes to skip.
   */
  skip(n) {
    if (this.offset + n > this.#view.byteLength) {
      throw new Error(`BufReader overrun: need ${n} bytes at offset ${this.offset}, have ${this.#view.byteLength}`)
    }
    this.offset += n
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
