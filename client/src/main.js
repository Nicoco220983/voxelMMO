// @ts-check
import { RenderManager } from './RenderManager.js'
import { GameClient } from './GameClient.js'
import { NetworkProtocol } from './NetworkProtocol.js'
import {
  SUBVOXEL_SIZE, TICK_RATE, EntityType,
} from './types.js'
import { Hotbar } from './ui/Hotbar.js'
import { VoxelHighlight } from './ui/VoxelHighlight.js'
import { BulkVoxelsSelection } from './ui/BulkVoxelsSelection.js'
import { createController } from './controllers/ControllerManager.js'
import { TouchController } from './controllers/TouchController.js'
import { KeyboardController } from './controllers/KeyboardController.js'

/** @typedef {import('./types.js').SubVoxelCoord} SubVoxelCoord */

// ── Graphics ──────────────────────────────────────────────────────────────
const rm = new RenderManager()
const renderer = rm.renderer
const scene    = rm.scene
const camera   = rm.camera
const composer = rm.composer
const ssaoPass = rm.ssaoPass

// ── Network ───────────────────────────────────────────────────────────────
// ?mode=ghost → GHOST_PLAYER (noclip); default (bare URL) → PLAYER (full physics)
const _mode = new URLSearchParams(location.search).get('mode')
const _entityType = _mode === 'ghost' ? EntityType.GHOST_PLAYER : EntityType.PLAYER

// Generate or retrieve session token for entity recovery across reconnects
function getOrCreateSessionToken() {
  const STORAGE_KEY = 'voxelmmo_session_token'
  let token = localStorage.getItem(STORAGE_KEY)
  if (!token) {
    // Generate 16-byte random token and encode as base64
    const bytes = new Uint8Array(16)
    crypto.getRandomValues(bytes)
    token = btoa(String.fromCharCode(...bytes))
    localStorage.setItem(STORAGE_KEY, token)
  }
  // Decode base64 back to Uint8Array
  const binary = atob(token)
  const bytes = new Uint8Array(16)
  for (let i = 0; i < 16; i++) {
    bytes[i] = binary.charCodeAt(i)
  }
  return bytes
}

const _sessionToken = getOrCreateSessionToken()

const client = new GameClient(`ws://${location.host}/ws`, scene)
window.gameClient = client  // Expose for entity prediction
client.connect().then(() => {
  client.sendJoin(_entityType, _sessionToken)
}).catch((err) => {
  console.error('[main] Failed to connect to server', err)
})

// ── Get local player from entity registry via client.selfEntity ───────────
/**
 * Get the local player entity from the entity registry.
 * Returns null until SELF_ENTITY message has been received.
 * @returns {import('./entities/BaseEntity.js').BaseEntity|null}
 */
function getLocalPlayer() {
  return client.selfEntity
}

// ── Hotbar ─────────────────────────────────────────────────────────────────
const hotbar = new Hotbar()

/** @type {import('./tools/Tool.js').Tool|null} */
let lastSelectedTool = null

// Handle tool unselection (ESC/BACK pressed)
hotbar.onToolUnselected = () => {
  lastSelectedTool = null
}

// ── Voxel Highlight System ────────────────────────────────────────────────
const voxelHighlight = new VoxelHighlight(scene)

// ── Bulk Voxels Selection ─────────────────────────────────────────────────
const bulkSelection = new BulkVoxelsSelection(scene)

// ── Controller (keyboard or touch) ────────────────────────────────────────
const controller = createController(renderer.domElement)
controller.setBulkSelection(bulkSelection)

// Hook up touch hotbar tap detection and ESC handling
if (controller instanceof TouchController) {
  // Override selectSlot to handle tap detection
  const originalSelectSlot = hotbar.selectSlot.bind(hotbar)
  hotbar.selectSlot = (index) => {
    controller.onHotbarTap(index)
    originalSelectSlot(index)
  }

  // Wire up back button to trigger ESC
  hotbar.onToolUnselected = () => {
    lastSelectedTool = null
    controller.selectedSlotIndex = null
  }
}

// ── Input sending ─────────────────────────────────────────────────────────

/** @type {number} */ let lastButtons = -1  // force first send (NaN-like)
/** @type {number} */ let lastYaw     = NaN
/** @type {number} */ let lastPitch   = NaN
/** @type {number} */ let lastInputType = -1

/**
 * Map hotbar slot index (0-9) to InputType value.
 * For now, all slots default to MOVE (standard movement).
 * Future: slot selection will determine action mode.
 * @param {number} slotIndex
 * @returns {number} InputType value
 */
function slotToInputType(slotIndex) {
  // For now, all inputs are MOVE type
  // Future logic: different slots may trigger different input types
  return 0 // InputType.MOVE
}

/** Send INPUT frame only when state changed. */
function sendInputIfChanged(buttons, yawVal, pitchVal) {
  const inputType = slotToInputType(hotbar.selectedIndex)
  if (buttons === lastButtons && yawVal === lastYaw && pitchVal === lastPitch && inputType === lastInputType) return
  client.sendInput(NetworkProtocol.serializeInputMove(buttons, yawVal, pitchVal))
  lastButtons = buttons; lastYaw = yawVal; lastPitch = pitchVal; lastInputType = inputType
}

// ── HUD ───────────────────────────────────────────────────────────────────
const hud = /** @type {HTMLElement} */ (document.getElementById('hud'))

// ── Render loop ───────────────────────────────────────────────────────────
let lastTime = performance.now()

function animate() {
  requestAnimationFrame(animate)

  const now = performance.now()
  const dt  = Math.min((now - lastTime) / 1000, 0.1)  // cap at 100 ms
  lastTime  = now

  // Read controller state before update() clears one-shot flags
  const buttons = controller.buttons
  const yaw     = controller.yaw
  const pitch   = controller.pitch

  // Handle Q key to unselect tool (and exit voxel mode if active)
  if (controller.unselectToolPressed) {
    hotbar.handleQ()
    controller.unselectToolPressed = false
  }

  // Handle hotbar keyboard input
  if (!(controller instanceof TouchController)) {
    // Pass key events to hotbar - but we need to hook this up differently
    // The hotbar's handleKeyDown is called via window event listener
  }

  // Hotbar selection from controller
  if (controller.selectedSlotIndex !== null && !(controller instanceof TouchController)) {
    hotbar.selectSlot(controller.selectedSlotIndex)
  }

  // Detect tool changes to trigger onSelect/onDeselect callbacks
  const currentSlot = hotbar.getSelectedSlot()
  const currentTool = currentSlot.tool
  if (currentTool !== lastSelectedTool) {
    if (lastSelectedTool && lastSelectedTool.onDeselect) {
      lastSelectedTool.onDeselect()
    }
    if (currentTool && currentTool.onSelect) {
      currentTool.onSelect()
    }
    lastSelectedTool = currentTool
  }

  // Get current target FIRST (needed for sync's long-press bulk entry)
  const toolMode = currentTool ? currentTool.getHighlightMode() : 'none'
  const toolColor = currentTool ? currentTool.getHighlightColor() : 0
  const currentTarget = controller.getCurrentTarget(toolMode, voxelHighlight, camera, client.chunkRegistry)

  // Process pending inputs (tool key presses with mode transitions)
  if (controller instanceof KeyboardController || controller instanceof TouchController) {
    controller.processPendingInputs(hotbar, voxelHighlight, client.chunkRegistry, camera)
  }

  // Sync controller state (auto-exit builder mode if tool changed, handle movement, etc.)
  // Note: long-press bulk entry happens here, uses currentTarget for start position
  controller.sync(hotbar, voxelHighlight, client.chunkRegistry, camera, currentTarget)

  // Update voxel highlight visualization
  voxelHighlight.setTarget(currentTarget, toolColor, toolMode, controller.isBuilderMode())

  // Send all pending input (tool activation and/or movement)
  controller.sendInput(client, hotbar, voxelHighlight, camera, client.chunkRegistry)

  // Sync bulk selection visuals
  bulkSelection.setColor(toolColor)
  if (controller.isBulkActive()) {
    const bulkTarget = controller.getBulkTarget(toolMode, voxelHighlight, camera, client.chunkRegistry)
    bulkSelection.updateEnd(bulkTarget)
  }

  // ── Position update: get from local player entity via registry ────────────
  const localPlayer = getLocalPlayer()
  let posX = 32 * SUBVOXEL_SIZE   // 8192 - default spawn X
  let posY = 22 * SUBVOXEL_SIZE   // 5632 - default spawn Y (approx surface + 2)
  let posZ = 32 * SUBVOXEL_SIZE   // 8192 - default spawn Z
  let predGrounded = false

  if (localPlayer) {
    // Use currentPos which is forward-predicted by PhysicsPredictionSystem.
    // This gives proper voxel collision, gravity, and grounded detection.
    const pos = localPlayer.currentPos
    posX = pos.x
    posY = pos.y
    posZ = pos.z
    predGrounded = localPlayer.motion.receivedGrounded
  }
  // Note: Before SELF_ENTITY arrives, camera stays at default spawn position.
  // The initial position is set by server-sent entity in chunk snapshot.

  // Divide by SUBVOXEL_SIZE to get voxel coordinates for Three.js.
  camera.position.set(posX / SUBVOXEL_SIZE, posY / SUBVOXEL_SIZE, posZ / SUBVOXEL_SIZE)
  camera.rotation.y = yaw
  camera.rotation.x = pitch

  // ── Entity updates ────────────────────────────────────────────────────
  // Update entity animations (sheep leg swing, player mesh position, etc.)
  client.updateEntities(dt)

  client.pruneDistantChunks(posX / SUBVOXEL_SIZE, posZ / SUBVOXEL_SIZE)
  client.rebuildDirtyChunks()

  composer.render()

  const vposX = posX / SUBVOXEL_SIZE, vposY = posY / SUBVOXEL_SIZE, vposZ = posZ / SUBVOXEL_SIZE
  const modeName = _entityType === EntityType.GHOST_PLAYER ? 'ghost' : 'walk'
  let builderIndicator = ''
  if (controller.isBulkActive()) {
    const phaseText = controller.getBulkPhase() === 'selecting_start' ? 'SELECT START' : 'SELECT END'
    builderIndicator = ` [BULK: ${phaseText}]`
  } else if (controller.isBuilderMode()) {
    builderIndicator = ' [BUILDER]'
  }

  // Add voxel mode indicator
  let voxelModeIndicator = ''
  if (hotbar.isInVoxelMode()) {
    const createVoxelTool = hotbar.getCreateVoxelTool()
    if (createVoxelTool) {
      const item = hotbar.voxelItems.find(i => i.type === createVoxelTool.getVoxelType())
      const voxelTypeName = item ? item.name : 'Unknown'
      voxelModeIndicator = ` [VOXEL: ${voxelTypeName}]`
    }
  }

  hud.textContent =
    `[${modeName}]${builderIndicator}${voxelModeIndicator}  pos  ${vposX.toFixed(1)}  ${vposY.toFixed(1)}  ${vposZ.toFixed(1)}` +
    `   yaw ${(yaw * 180 / Math.PI).toFixed(0)}°`

  // Update controller at end of frame (resets one-shot flags)
  controller.update(dt)
}

// Hook up keyboard events for hotbar
window.addEventListener('keydown', (e) => {
  // Handle Q key for unselecting tool
  if (e.code === 'KeyQ') {
    if (hotbar.handleQ(e)) {
      // Tool was unselected
      if (!(controller instanceof TouchController)) {
        controller.selectedSlotIndex = null
      }
    }
    return
  }

  if (hotbar.handleKeyDown(e)) {
    // Hotbar handled it, update controller state
    const slot = hotbar.getSelectedSlot()
    if (slot.index >= 0) {
      if (!(controller instanceof TouchController)) {
        // For keyboard controller, we handle this via selectedSlotIndex
        // But the hotbar already processed it, so just sync
      }
    } else {
      // Tool was unselected
      if (!(controller instanceof TouchController)) {
        controller.selectedSlotIndex = null
      }
    }
  }
})

animate()
