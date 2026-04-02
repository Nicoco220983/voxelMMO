// @ts-check

/**
 * @typedef {import('../ui/VoxelHighlight.js').VoxelHighlight} VoxelHighlight
 * @typedef {import('../ui/BulkVoxelsSelection.js').BulkVoxelsSelection} BulkVoxelsSelection
 */

/**
 * Unified state machine for input modes.
 * Handles NORMAL → BUILDER → BULK transitions and target management.
 */
export class InputModeManager {
  /** Normal mode: raycast-based targeting */
  static NORMAL = 'normal'
  /** Builder mode: keyboard-controlled voxel target */
  static BUILDER = 'builder'

  /** @type {'normal'|'builder'} */
  #mode = InputModeManager.NORMAL

  /** Player yaw at builder mode entry time (for movement orientation)
   * @type {number}
   */
  #entryYaw = 0

  /** Builder mode target (keyboard-controlled voxel position)
   * @type {{x: number, y: number, z: number}|null}
   */
  #builderTarget = null

  /** Bulk voxel selection state/visual manager
   * @type {BulkVoxelsSelection|null}
   */
  #bulkSelection = null

  /**
   * True for one frame when mode changes (in either direction).
   * @type {boolean}
   */
  modeChanged = false

  /**
   * Builder movement delta for this frame (-1, 0, or 1 per axis).
   * Only set when in builder mode.
   * @type {{x: number, y: number, z: number}}
   */
  builderMoveDelta = { x: 0, y: 0, z: 0 }

  /**
   * Reset one-shot flags. Call at start of each frame.
   */
  update() {
    this.modeChanged = false
    this.builderMoveDelta = { x: 0, y: 0, z: 0 }
  }

  /**
   * Get current mode.
   * @returns {'normal'|'builder'}
   */
  getMode() {
    return this.#mode
  }

  /**
   * Get current bulk phase.
   * @returns {'idle'|'selecting_start'|'selecting_end'}
   */
  getBulkPhase() {
    return this.#bulkSelection?.getBulkPhase() ?? 'idle'
  }

  /**
   * Check if currently in builder mode.
   * @returns {boolean}
   */
  isBuilderMode() {
    return this.#mode === InputModeManager.BUILDER
  }

  /**
   * Check if bulk mode is active (any phase).
   * @returns {boolean}
   */
  isBulkActive() {
    return this.#bulkSelection?.isActive() ?? false
  }

  /**
   * Get entry yaw (for builder mode movement orientation).
   * @returns {number}
   */
  getEntryYaw() {
    return this.#entryYaw
  }

  /**
   * Get builder target position.
   * @returns {{x: number, y: number, z: number}|null}
   */
  getBuilderTarget() {
    return this.#builderTarget
  }

  /**
   * Get bulk selection start position.
   * @returns {{x: number, y: number, z: number}|null}
   */
  getBulkStart() {
    return this.#bulkSelection?.getStartPos() ?? null
  }

  /**
   * Set the bulk selection manager.
   * @param {BulkVoxelsSelection|null} bulkSelection
   */
  setBulkSelection(bulkSelection) {
    this.#bulkSelection = bulkSelection
  }

  /**
   * Enter builder mode.
   * @param {number} entryYaw - Player yaw at entry time
   * @param {{x: number, y: number, z: number}|null} initialTarget - Initial target from raycast
   * @returns {boolean} true if mode changed
   */
  enterBuilderMode(entryYaw, initialTarget) {
    if (this.#mode === InputModeManager.BUILDER) return false

    this.#mode = InputModeManager.BUILDER
    this.#entryYaw = entryYaw
    this.#builderTarget = initialTarget
    this.modeChanged = true
    return true
  }

  /**
   * Exit builder mode (return to normal).
   * Also exits bulk mode if active.
   * @returns {boolean} true if mode changed
   */
  exitBuilderMode() {
    const wasBuilder = this.#mode === InputModeManager.BUILDER
    this.#mode = InputModeManager.NORMAL
    this.#builderTarget = null
    this.exitBulkMode()

    if (wasBuilder) {
      this.modeChanged = true
      return true
    }
    return false
  }

  /**
   * Activate bulk mode without setting start (triple-tap entry).
   * Can be used from either normal or builder mode.
   * @param {'destroy'|'create'} [toolMode='destroy']
   * @returns {boolean} true if activated
   */
  activateBulkMode(toolMode = 'destroy') {
    this.#bulkSelection?.activate(toolMode)
    return true
  }

  /**
   * Enter bulk mode and immediately set start position (long-press entry).
   * Can be used from either normal or builder mode.
   * @param {{x: number, y: number, z: number}} startPos
   * @param {'destroy'|'create'} [toolMode='destroy']
   * @returns {boolean} true if activated
   */
  enterBulkModeWithStart(startPos, toolMode = 'destroy') {
    this.#bulkSelection?.start(startPos, toolMode)
    return true
  }

  /**
   * Exit bulk mode (clear all bulk state).
   */
  exitBulkMode() {
    this.#bulkSelection?.exit()
  }

  /**
   * Handle bulk action click.
   * Delegates to BulkVoxelsSelection state machine.
   * @param {{x: number, y: number, z: number}|null} targetPos
   * @returns {{consumed: boolean, complete: boolean, start: {x: number, y: number, z: number}|null, end: {x: number, y: number, z: number}|null}}
   */
  onBulkAction(targetPos) {
    return this.#bulkSelection?.onAction(targetPos) ?? { consumed: false, complete: false, start: null, end: null }
  }

  /**
   * Move builder target by delta.
   * @param {number} dx
   * @param {number} dy
   * @param {number} dz
   */
  moveBuilderTarget(dx, dy, dz) {
    if (!this.#builderTarget) return
    this.#builderTarget.x += dx
    this.#builderTarget.y += dy
    this.#builderTarget.z += dz
  }

  /**
   * Set builder target directly (e.g., from raycast on mode entry).
   * @param {{x: number, y: number, z: number}|null} target
   */
  setBuilderTarget(target) {
    this.#builderTarget = target
  }

  /**
   * Get the current target based on mode.
   * Unified API - callers don't need to know which mode is active.
   * @param {'destroy'|'create'} toolMode
   * @param {VoxelHighlight} highlightSystem
   * @param {import('three').PerspectiveCamera} camera
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   * @returns {{x: number, y: number, z: number}|null}
   */
  getTarget(toolMode, highlightSystem, camera, chunkRegistry) {
    if (this.#mode === InputModeManager.BUILDER) {
      return this.#builderTarget
    }

    // Normal mode: do raycast to get target
    return highlightSystem.raycastTarget(camera, chunkRegistry, toolMode)
  }

  /**
   * Get current target for bulk selection preview.
   * @param {'destroy'|'create'} toolMode
   * @param {VoxelHighlight} highlightSystem
   * @param {import('three').PerspectiveCamera} camera
   * @param {import('../ChunkRegistry.js').ChunkRegistry} chunkRegistry
   * @returns {{x: number, y: number, z: number}|null}
   */
  getBulkTarget(toolMode, highlightSystem, camera, chunkRegistry) {
    if (this.#mode === InputModeManager.BUILDER) {
      return this.#builderTarget
    }

    return highlightSystem.raycastTarget(camera, chunkRegistry, toolMode)
  }

  /**
   * Set builder movement delta.
   * Called by controllers when in builder mode.
   * @param {number} dx
   * @param {number} dy
   * @param {number} dz
   */
  setBuilderMoveDelta(dx, dy, dz) {
    this.builderMoveDelta = { x: dx, y: dy, z: dz }
  }
}
