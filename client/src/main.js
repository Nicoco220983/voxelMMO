// @ts-check
import * as THREE     from 'three'
import { GameClient } from './GameClient.js'
import { NetworkProtocol, InputType } from './NetworkProtocol.js'
import {
  SUBVOXEL_SIZE, TICK_RATE, EntityType,
} from './types.js'
import { Hotbar } from './ui/Hotbar.js'
import { VoxelType } from './types.js'
import { DestroyVoxelTool } from './tools/DestroyVoxelTool.js'
import { CreateVoxelTool } from './tools/CreateVoxelTool.js'
import { VoxelHighlightSystem } from './systems/VoxelHighlightSystem.js'
import { createController } from './controllers/ControllerManager.js'
import { TouchController } from './controllers/TouchController.js'

/** @typedef {import('./types.js').SubVoxelCoord} SubVoxelCoord */

// ── Renderer ──────────────────────────────────────────────────────────────
const renderer = new THREE.WebGLRenderer({ antialias: true })
renderer.setSize(window.innerWidth, window.innerHeight)
renderer.setPixelRatio(devicePixelRatio)
document.body.appendChild(renderer.domElement)

// ── Scene ─────────────────────────────────────────────────────────────────
const scene = new THREE.Scene()
scene.background = new THREE.Color(0x87ceeb)
scene.fog = new THREE.Fog(0x87ceeb, 200, 600)

// ── Camera ────────────────────────────────────────────────────────────────
const camera = new THREE.PerspectiveCamera(
  75, window.innerWidth / window.innerHeight, 0.1, 1000)
camera.rotation.order = 'YXZ'

// ── Lights ────────────────────────────────────────────────────────────────
scene.add(new THREE.AmbientLight(0xffffff, 0.5))
const sun = new THREE.DirectionalLight(0xffffff, 0.8)
sun.position.set(200, 400, 100)
scene.add(sun)

// ── Resize handler ────────────────────────────────────────────────────────
window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight
  camera.updateProjectionMatrix()
  renderer.setSize(window.innerWidth, window.innerHeight)
})

// ── Network ───────────────────────────────────────────────────────────────
// ?mode=ghost → GHOST_PLAYER (noclip); default (bare URL) → PLAYER (full physics)
const _mode = new URLSearchParams(location.search).get('mode')
const _entityType = _mode === 'ghost' ? EntityType.GHOST_PLAYER : EntityType.PLAYER

const client = new GameClient(`ws://${location.host}/ws`, scene)
window.gameClient = client  // Expose for entity prediction
client.connect().then(() => {
  client.sendJoin(_entityType)
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
// Assign tools to slots (slot index 0 = key "1", slot 1 = key "2", etc.)
hotbar.setSlot(1, new DestroyVoxelTool())   // Key "2"
hotbar.setSlot(2, new CreateVoxelTool(VoxelType.BASIC))  // Key "3"

// ── Voxel Highlight System ────────────────────────────────────────────────
const voxelHighlight = new VoxelHighlightSystem(scene)

// ── Controller (keyboard or touch) ────────────────────────────────────────
const controller = createController(renderer.domElement)

// Hook up touch hotbar double-tap detection (must be after controller creation)
if (controller instanceof TouchController) {
  const originalSelectSlot = hotbar.selectSlot.bind(hotbar)
  hotbar.selectSlot = (index) => {
    controller.onHotbarTap(index)
    originalSelectSlot(index)
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
  return InputType.MOVE
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

  // Hotbar selection from controller (keyboard only - touch handled via hook)
  if (controller.selectedSlotIndex !== null && !(controller instanceof TouchController)) {
    hotbar.selectSlot(controller.selectedSlotIndex)
  }

  // Sync controller state (auto-exit builder mode if tool changed, etc.)
  controller.sync(hotbar)

  // Sync voxel highlight visuals with controller and hotbar state
  voxelHighlight.sync(camera, hotbar, controller, client.chunkRegistry)

  // Update builder target from highlight system (needed for tool operations)
  controller.builderTarget = voxelHighlight.getBuilderTarget()

  // Handle bulk preview visualization
  if (controller.bulkBuilderMode) {
    if (controller.bulkPhase === 'none') {
      voxelHighlight.setBulkPreview(null, false)
    } else if (controller.bulkPhase === 'start') {
      voxelHighlight.setBulkPreview(controller.bulkStartVoxel, true)
    }
  }

  // Handle tool activation (clicks) - controller sends the input
  if (controller.toolActivated) {
    controller.sendToolInput(client, hotbar, voxelHighlight)
  }

  // Handle builder mode voxel movement OR send player movement
  if (controller.builderMode) {
    // In builder mode: apply movement delta to highlighted voxel
    const delta = controller.builderMoveDelta
    if (delta.x !== 0 || delta.y !== 0 || delta.z !== 0) {
      voxelHighlight.moveBuilderTarget(delta.x, delta.y, delta.z)
    }
  } else {
    // Normal mode: send player movement to server
    sendInputIfChanged(buttons, yaw, pitch)
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

  renderer.render(scene, camera)

  const vposX = posX / SUBVOXEL_SIZE, vposY = posY / SUBVOXEL_SIZE, vposZ = posZ / SUBVOXEL_SIZE
  const modeName = _entityType === EntityType.GHOST_PLAYER ? 'ghost' : 'walk'
  let builderIndicator = ''
  if (controller.bulkBuilderMode) {
    const phaseText = controller.bulkPhase === 'none' ? 'SELECT START' : 'SELECT END'
    builderIndicator = ` [BULK: ${phaseText}]`
  } else if (controller.builderMode) {
    builderIndicator = ' [BUILDER]'
  }
  hud.textContent =
    `[${modeName}]${builderIndicator}  pos  ${vposX.toFixed(1)}  ${vposY.toFixed(1)}  ${vposZ.toFixed(1)}` +
    `   yaw ${(yaw * 180 / Math.PI).toFixed(0)}°`

  // Update controller at end of frame (resets one-shot flags)
  controller.update(dt)
}

animate()
