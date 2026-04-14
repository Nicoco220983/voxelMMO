// @ts-check
import * as THREE from 'three'
import { Tool } from './Tool.js'
import { ToolType } from '../ToolCatalog.js'
import { NetworkProtocol } from '../NetworkProtocol.js'

/**
 * Hand tool - server-authoritative combat tool.
 * All constants are static (class-level) for easy access without instantiation.
 */
export class HandTool extends Tool {
  // Tool constants (static for access without instance)
  static TOOL_ID = ToolType.HAND
  static DAMAGE = 5
  static COOLDOWN_TICKS = 10    // 0.5 seconds at 20tps
  static RANGE = 3.0            // voxels
  static KNOCKBACK = 200        // sub-voxels/tick impulse
  static ICON = '✊'
  static NAME = 'Hand'
  
  constructor() {
    super(HandTool.NAME, HandTool.ICON)
  }
  
  /**
   * Static: Get the tool type ID.
   * @returns {number}
   */
  static getToolTypeStatic() {
    return HandTool.TOOL_ID
  }
  
  /**
   * Returns true if this tool targets entities (combat).
   * @returns {boolean}
   */
  /**
   * Static: Returns true if this tool targets entities (combat).
   * @returns {boolean}
   */
  static targetsEntitiesStatic() {
    return true
  }

  targetsEntities() {
    return HandTool.targetsEntitiesStatic()
  }
  
  /**
   * Get cooldown duration in milliseconds.
   * @returns {number}
   */
  getCooldownMs() {
    // 20 ticks per second = 50ms per tick
    return HandTool.COOLDOWN_TICKS * 50
  }
  
  /**
   * Create the first-person visual for the hand tool.
   * Static method - no instance needed.
   * @returns {import('three').Object3D}
   */
  static createFirstPersonVisual() {
    const group = new THREE.Group()
    group.name = 'HandToolVisual'
    
    // Skin-colored material for the hand
    const skinMat = new THREE.MeshLambertMaterial({ color: 0xffccaa })
    
    // Main fist (palm)
    const palmGeo = new THREE.BoxGeometry(0.12, 0.14, 0.12)
    const palm = new THREE.Mesh(palmGeo, skinMat)
    palm.position.set(0, 0, 0)
    group.add(palm)
    
    // Thumb (offset to side)
    const thumbGeo = new THREE.BoxGeometry(0.04, 0.08, 0.04)
    const thumb = new THREE.Mesh(thumbGeo, skinMat)
    thumb.position.set(0.08, -0.02, 0.02)
    thumb.rotation.z = -0.3
    group.add(thumb)
    
    // Fingers (curled)
    const fingerGeo = new THREE.BoxGeometry(0.035, 0.08, 0.035)
    
    // Index finger
    const index = new THREE.Mesh(fingerGeo, skinMat)
    index.position.set(0.03, 0.08, 0.02)
    index.rotation.x = -0.2
    group.add(index)
    
    // Middle finger
    const middle = new THREE.Mesh(fingerGeo, skinMat)
    middle.position.set(0, 0.09, 0.02)
    middle.rotation.x = -0.2
    group.add(middle)
    
    // Ring finger
    const ring = new THREE.Mesh(fingerGeo, skinMat)
    ring.position.set(-0.03, 0.08, 0.02)
    ring.rotation.x = -0.2
    group.add(ring)
    
    return group
  }
  
  /**
   * Get animation offset for first-person visual.
   * Returns how far forward the hand should punch during cooldown.
   * 
   * @param {number} lastUsedTick - Server tick when hand was last used
   * @param {number} currentTick - Current server render tick
   * @returns {number} Animation offset in voxels (0 = idle, max 0.25)
   */
  static getAnimationOffset(lastUsedTick, currentTick) {
    const cooldownTicks = HandTool.COOLDOWN_TICKS
    const ticksSinceUse = currentTick - lastUsedTick
    
    if (ticksSinceUse >= cooldownTicks) {
      return 0  // Animation complete
    }
    
    // Calculate animation phase (0 to 1 during cooldown)
    const phase = ticksSinceUse / cooldownTicks
    
    // Punch curve: fast forward (first 30%), slower back (remaining 70%)
    let punchAmount
    if (phase < 0.3) {
      // Forward punch (0 to 1 in first 30%)
      punchAmount = phase / 0.3
    } else {
      // Return to idle (1 to 0 in remaining 70%)
      punchAmount = 1 - ((phase - 0.3) / 0.7)
    }
    
    // Smooth easing and scale to max offset
    return Math.sin(punchAmount * Math.PI) * 0.25
  }
  
  /**
   * Instance method wrapper for compatibility.
   * @param {import('three').Scene} scene
   * @returns {import('three').Object3D}
   */
  createVisual(scene) {
    return HandTool.createFirstPersonVisual(scene)
  }
  
  /**
   * Serialize input for tool use (entity attack).
   * Static method - can be called without instance.
   * @param {number} yaw
   * @param {number} pitch
   * @returns {ArrayBuffer}
   */
  static serializeInput(yaw, pitch) {
    return NetworkProtocol.serializeInputToolUse(HandTool.TOOL_ID, yaw, pitch)
  }
  
  /**
   * Instance method for compatibility.
   * @param {import('../ui/VoxelHighlight.js').VoxelHighlight} highlightSystem
   * @returns {null}
   */
  onClick(highlightSystem) {
    // Hand tool targets entities, not voxels
    // Input is sent separately via serializeInput
    return null
  }
}
