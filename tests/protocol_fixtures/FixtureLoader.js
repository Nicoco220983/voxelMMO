// @ts-check
/**
 * Protocol fixture loader for JavaScript/Node.js tests.
 * 
 * Loads hex fixture files shared with C++ tests for cross-platform
 * protocol validation.
 * 
 * @example
 * import { loadHexFixture, loadInputFixture } from './FixtureLoader.js'
 * 
 * const bytes = loadHexFixture('client_to_server/input/input_forward.hex')
 * const inputData = loadInputFixture('input_forward.hex')
 */

import * as fs from 'fs'
import * as path from 'path'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

/**
 * Get the protocol fixtures directory path.
 * @returns {string} Absolute path to protocol_fixtures/
 */
export function getFixturesDirectory() {
  // The fixtures are in the same directory as this file
  return __dirname
}

/**
 * Parse a line of hex bytes, ignoring comments.
 * @param {string} line 
 * @returns {number[]}
 */
function parseHexLine(line) {
  const bytes = []
  // Stop at comment
  const commentIdx = line.indexOf('#')
  const content = commentIdx >= 0 ? line.slice(0, commentIdx) : line
  
  const parts = content.trim().split(/\s+/)
  for (const part of parts) {
    if (!part) continue
    if (part.length !== 2) {
      throw new Error(`Invalid hex byte: "${part}"`)
    }
    const value = parseInt(part, 16)
    if (isNaN(value) || value < 0 || value > 255) {
      throw new Error(`Invalid hex byte: "${part}"`)
    }
    bytes.push(value)
  }
  
  return bytes
}

/**
 * Load a hex fixture file as a Uint8Array.
 * 
 * Hex files contain space-separated hex bytes, with # comments ignored.
 * 
 * @param {string} relativePath Path relative to protocol_fixtures/ (e.g., "client_to_server/input/input_forward.hex")
 * @returns {Uint8Array} Parsed bytes
 * @throws {Error} If file not found or parse error
 */
export function loadHexFixture(relativePath) {
  const filepath = path.join(getFixturesDirectory(), relativePath)
  
  let content
  try {
    content = fs.readFileSync(filepath, 'utf-8')
  } catch (err) {
    throw new Error(`Cannot read fixture file: ${filepath}: ${err}`)
  }
  
  const bytes = []
  const lines = content.split('\n')
  
  for (const line of lines) {
    const trimmed = line.trim()
    // Skip empty lines and full-line comments
    if (!trimmed || trimmed.startsWith('#')) {
      continue
    }
    
    bytes.push(...parseHexLine(trimmed))
  }
  
  return new Uint8Array(bytes)
}

/**
 * Load an INPUT message fixture and parse its contents.
 * @param {string} filename Just the filename (e.g., "input_forward.hex")
 * @returns {{ bytes: Uint8Array, inputType: number, buttons: number, yaw: number, pitch: number }}
 */
export function loadInputFixture(filename) {
  const bytes = loadHexFixture(`client_to_server/input/${filename}`)
  
  if (bytes.length !== 14) {
    throw new Error(`INPUT fixture ${filename} should be 14 bytes, got ${bytes.length}`)
  }
  
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  const msgType = view.getUint8(0)
  const size = view.getUint16(1, true)
  const inputType = view.getUint8(3)
  const buttons = view.getUint8(4)
  const yaw = view.getFloat32(5, true)
  const pitch = view.getFloat32(9, true)
  
  if (msgType !== 0) {
    throw new Error(`INPUT fixture ${filename} should have type=0, got ${msgType}`)
  }
  if (size !== 14) {
    throw new Error(`INPUT fixture ${filename} should have size=14, got ${size}`)
  }
  
  return { bytes, inputType, buttons, yaw, pitch }
}

/**
 * Load a JOIN message fixture and parse its contents.
 * @param {string} filename Just the filename (e.g., "join_player.hex")
 * @returns {{ bytes: Uint8Array, entityType: number }}
 */
export function loadJoinFixture(filename) {
  const bytes = loadHexFixture(`client_to_server/join/${filename}`)
  
  if (bytes.length !== 5) {
    throw new Error(`JOIN fixture ${filename} should be 5 bytes, got ${bytes.length}`)
  }
  
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  const msgType = view.getUint8(0)
  const size = view.getUint16(1, true)
  const entityType = view.getUint8(3)
  
  if (msgType !== 1) {
    throw new Error(`JOIN fixture ${filename} should have type=1, got ${msgType}`)
  }
  if (size !== 5) {
    throw new Error(`JOIN fixture ${filename} should have size=5, got ${size}`)
  }
  
  return { bytes, entityType }
}

/**
 * Load a SELF_ENTITY message fixture and parse its contents.
 * @param {string} filename Just the filename (e.g., "self_entity_sample.hex")
 * @returns {{ bytes: Uint8Array, entityId: number, tick: number }}
 */
export function loadSelfEntityFixture(filename) {
  const bytes = loadHexFixture(`server_to_client/self_entity/${filename}`)
  
  if (bytes.length !== 13) {
    throw new Error(`SELF_ENTITY fixture ${filename} should be 13 bytes, got ${bytes.length}`)
  }
  
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  const msgType = view.getUint8(0)
  const size = view.getUint16(1, true)
  const entityId = view.getUint32(3, true)
  const tick = view.getUint32(7, true)
  // reserved field at bytes 11-12 is ignored
  
  if (msgType !== 6) {
    throw new Error(`SELF_ENTITY fixture ${filename} should have type=6, got ${msgType}`)
  }
  if (size !== 13) {
    throw new Error(`SELF_ENTITY fixture ${filename} should have size=13, got ${size}`)
  }
  
  return { bytes, entityId, tick }
}

/**
 * Compare two Uint8Arrays for equality.
 * @param {Uint8Array} a 
 * @param {Uint8Array} b 
 * @returns {boolean}
 */
export function bytesEqual(a, b) {
  if (a.length !== b.length) return false
  for (let i = 0; i < a.length; i++) {
    if (a[i] !== b[i]) return false
  }
  return true
}

/**
 * Format a Uint8Array as a hex string (space-separated).
 * @param {Uint8Array} bytes 
 * @returns {string}
 */
export function formatBytes(bytes) {
  return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join(' ')
}
