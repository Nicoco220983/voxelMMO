// @ts-check
import * as THREE from 'three'

/**
 * @typedef {import('../ui/VoxelHighlight.js').VoxelHighlight} VoxelHighlight
 */

/**
 * Base class for all tools.
 * Tools define their name and icon, but don't know their slot position.
 * Tools return serialized input data; the caller (Controller) decides when to send.
 * 
 * Also provides static methods for first-person visual management:
 * - Tool.updateVisualSystem() - call each frame from main.js
 * @abstract
 */
export class Tool {
  /** @type {string} Display name of the tool */
  name
  
  /** @type {string} Icon/emoji for the tool */
  icon

  /**
   * @param {string} name - Display name of the tool
   * @param {string} icon - Icon/emoji for the tool
   */
  constructor(name, icon) {
    this.name = name
    this.icon = icon
  }

  // --- Static Visual System State ---

  /** @type {import('three').Object3D|null} Current first-person mesh */
  static #currentMesh = null

  /** @type {import('three').Scene|null} Scene reference */
  static #scene = null

  /** @type {import('three').Camera|null} Camera reference */
  static #camera = null

  /** @type {number} Currently active tool ID */
  static #currentToolId = -1

  /** @type {number} Last used tick for animation */
  static #lastUsedTick = 0

  /**
   * Initialize the visual system with scene and camera.
   * Call once at startup.
   * @param {import('three').Scene} scene
   * @param {import('three').Camera} camera
   */
  static initVisualSystem(scene, camera) {
    Tool.#scene = scene
    Tool.#camera = camera
  }

  /**
   * Update the visual system - call each frame from main.js.
   * @param {number} toolId - Current tool ID
   * @param {number} lastUsedTick - Server tick when tool was last used
   * @param {number} currentTick - Current render tick
   */
  static updateVisualSystem(toolId, lastUsedTick, currentTick) {
    if (!Tool.#scene || !Tool.#camera) return

    // Check if tool changed
    if (toolId !== Tool.#currentToolId) {
      Tool.#onToolChanged(toolId)
    }

    Tool.#lastUsedTick = lastUsedTick

    // Update mesh position if exists
    if (Tool.#currentMesh) {
      Tool.#updateMeshPosition(toolId, lastUsedTick, currentTick)
    }
  }

  /**
   * Clean up visual system resources.
   */
  static destroyVisualSystem() {
    if (Tool.#currentMesh && Tool.#scene) {
      Tool.#scene.remove(Tool.#currentMesh)
      Tool.#currentMesh = null
    }
    Tool.#scene = null
    Tool.#camera = null
  }

  /**
   * @private
   */
  static #onToolChanged(newToolId) {
    const OldToolClass = Tool.getToolClass(Tool.#currentToolId)
    const NewToolClass = Tool.getToolClass(newToolId)

    // Notify old tool it's becoming inactive
    OldToolClass?.onToolInactive?.(Tool.#currentMesh)

    // Remove old mesh
    if (Tool.#currentMesh && Tool.#scene) {
      Tool.#scene.remove(Tool.#currentMesh)
      Tool.#currentMesh = null
    }

    // Create new mesh if new tool has a visual
    if (NewToolClass?.createFirstPersonVisual) {
      Tool.#currentMesh = NewToolClass.createFirstPersonVisual()
      if (Tool.#currentMesh && Tool.#scene) {
        Tool.#scene.add(Tool.#currentMesh)
      }
    }

    // Notify new tool it's becoming active
    NewToolClass?.onToolActive?.(Tool.#currentMesh)

    Tool.#currentToolId = newToolId
  }

  /**
   * @private
   */
  static #updateMeshPosition(toolId, lastUsedTick, currentTick) {
    if (!Tool.#currentMesh || !Tool.#camera) return

    // Get animation offset from current tool class
    const ToolClass = Tool.getToolClass(toolId)
    const animOffset = ToolClass?.getAnimationOffset
      ? ToolClass.getAnimationOffset(lastUsedTick, currentTick)
      : 0

    // Camera-relative positioning
    const camPos = Tool.#camera.position
    const camDir = new THREE.Vector3(0, 0, -1).applyQuaternion(Tool.#camera.quaternion)
    const right = new THREE.Vector3(1, 0, 0).applyQuaternion(Tool.#camera.quaternion)
    const up = new THREE.Vector3(0, 1, 0).applyQuaternion(Tool.#camera.quaternion)

    // Base position: arm's length forward, right and down, plus animation
    const offset = camDir.clone().multiplyScalar(0.5 + animOffset)
      .add(right.clone().multiplyScalar(0.25))
      .add(up.clone().multiplyScalar(-0.2))

    Tool.#currentMesh.position.copy(camPos).add(offset)
    Tool.#currentMesh.quaternion.copy(Tool.#camera.quaternion)
  }

  /**
   * Get ToolClass by ID. Override in subclass to provide tool catalog.
   * @param {number} toolId
   * @returns {typeof Tool|null}
   * @abstract
   */
  static getToolClass(toolId) {
    // Base Tool class doesn't know about specific tools
    // This is overridden or replaced by ToolCatalog integration
    return null
  }

  /**
   * Called when the tool is selected (player switches to this tool).
   * @param {VoxelHighlight} highlightSystem
   */
  onSelect(highlightSystem) {
    // Override in subclass if needed
  }

  /**
   * Called when the tool is deselected (player switches to another tool).
   * @param {VoxelHighlight} highlightSystem
   */
  onDeselect(highlightSystem) {
    // Override in subclass if needed
  }

  /**
   * Called when the player clicks (left mouse button) while this tool is active.
   * Returns serialized input data, or null if no action should be taken.
   * @abstract
   * @param {VoxelHighlight} highlightSystem
   * @returns {ArrayBuffer|null}
   */
  onClick(highlightSystem) {
    throw new Error('Tool.onClick() must be implemented by subclass')
  }

  /**
   * Called every frame to update tool-specific visuals (e.g., paste preview).
   * Override in subclass if needed.
   * @param {VoxelHighlight} highlightSystem
   * @param {boolean} isBuilderMode
   * @param {{x: number, y: number, z: number}|null} builderTarget
   * @param {{x: number, y: number, z: number}|null} currentTarget
   * @param {number} toolColor
   */
  update(highlightSystem, isBuilderMode, builderTarget, currentTarget, toolColor) {
    // Default: clear any preview
    highlightSystem.setPreviewVoxels([], 0)
  }

  /**
   * Get the highlight mode for this tool.
   * @returns {'destroy'|'create'|'select'|'none'} What kind of highlighting to show
   */
  getHighlightMode() {
    return 'none'
  }

  /**
   * Get the highlight color for this tool.
   * @returns {number} Hex color value (0xRRGGBB)
   */
  getHighlightColor() {
    return 0xFFFFFF  // Default white
  }

  /**
   * Returns true if this tool supports builder mode.
   * @returns {boolean}
   */
  supportsBuilderMode() {
    return false
  }

  /**
   * Returns true if this tool supports bulk mode.
   * @returns {boolean}
   */
  supportsBulkMode() {
    return false
  }

  /**
   * Returns true if selecting this tool should switch the hotbar into voxel mode.
   * @returns {boolean}
   */
  needsVoxelMode() {
    return false
  }

  /**
   * Returns true if selecting this tool should switch the hotbar into select mode.
   * @returns {boolean}
   */
  needsSelectMode() {
    return false
  }

  // --- Server-Authoritative Tool Interface ---

  /**
   * Static: Get the tool type ID for this tool class.
   * Override in server-authoritative tool classes.
   * @returns {number|null}
   */
  static getToolTypeStatic() {
    return null
  }

  /**
   * Instance: Get the tool type ID for this tool instance.
   * Delegates to static method by default.
   * 
   * - Server-authoritative tools (HandTool, etc.): return ToolType.HAND
   * - Client-side tools (VoxelTool): return null
   * 
   * This determines whether selecting the tool requires server confirmation
   * (for combat/sync) or can be applied immediately client-side.
   * 
   * @returns {number|null} ToolType for server tools, null for client tools
   */
  getToolType() {
    return this.constructor.getToolTypeStatic()
  }

  /**
   * Static: Get animation offset for first-person visual.
   * Returns how far forward (in voxels) the tool should be offset during animation.
   * @param {number} lastUsedTick - Server tick when tool was last used
   * @param {number} currentTick - Current server render tick
   * @returns {number} Animation offset in voxels (0 = idle)
   */
  static getAnimationOffset(lastUsedTick, currentTick) {
    return 0  // Default: no animation
  }

  /**
   * Returns true if this tool targets entities (not voxels).
   * @returns {boolean}
   */
  targetsEntities() {
    return false
  }

  /**
   * Get cooldown duration in milliseconds.
   * @returns {number}
   */
  getCooldownMs() {
    return 0
  }

  // --- First-Person Visual Interface ---

  /**
   * Static: Create the Three.js mesh for first-person view.
   * Override in subclass for custom visuals.
   * @returns {import('three').Object3D|null}
   */
  static createFirstPersonVisual() {
    return null
  }

  /**
   * Static: Get animation offset for first-person visual.
   * Returns how far forward (in voxels) the tool should be offset during animation.
   * @param {number} lastUsedTick - Server tick when tool was last used
   * @param {number} currentTick - Current server render tick
   * @returns {number} Animation offset in voxels (0 = idle)
   */
  static getAnimationOffset(lastUsedTick, currentTick) {
    return 0
  }

  /**
   * Static: Called when tool becomes active (selected).
   * Use to set up initial visual state.
   * @param {import('three').Object3D|null} mesh - The tool's first-person mesh, or null
   */
  static onToolActive(mesh) {
    // Override in subclass if needed
  }

  /**
   * Static: Called when tool becomes inactive (deselected).
   * Use to clean up visual state.
   * @param {import('three').Object3D|null} mesh - The tool's first-person mesh, or null
   */
  static onToolInactive(mesh) {
    // Override in subclass if needed
  }

  /**
   * Serialize input for builder mode activation.
   * Default implementation delegates to onClick with a mock highlight system.
   * Override for tools that need special builder mode handling.
   * @param {{x: number, y: number, z: number}} targetVoxel
   * @returns {ArrayBuffer|Array<ArrayBuffer>|null}
   */
  serializeBuilderInput(targetVoxel) {
    // Create a mock highlight system that returns our target
    const mockHighlight = {
      getCurrentTarget: () => targetVoxel,
    }
    return this.onClick(mockHighlight)
  }

  /**
   * Handle bulk action completion (when user selects start and end positions).
   * Override for tools that need custom bulk behavior.
   * Default implementation uses serializeBulkInput.
   * @param {{x: number, y: number, z: number}} start
   * @param {{x: number, y: number, z: number}} end
   * @returns {ArrayBuffer|Array<ArrayBuffer>|null} Input(s) to send to server
   */
  onBulkComplete(start, end) {
    return this.serializeBulkInput?.(start.x, start.y, start.z, end.x, end.y, end.z) ?? null
  }
}

/**
 * Default tool that does nothing (for unassigned hotbar slots).
 */
export class EmptyTool extends Tool {
  constructor() {
    super('Empty', '')
  }

  /**
   * @param {VoxelHighlight} highlightSystem
   * @returns {null}
   */
  onClick(highlightSystem) {
    return null
  }
}
