// @ts-check
import * as THREE     from 'three'
import { GameClient } from './GameClient.js'
import { NetworkProtocol, InputButton, InputType } from './NetworkProtocol.js'
import {
  SUBVOXEL_SIZE, TICK_RATE, EntityType,
} from './types.js'
import { Hotbar } from './ui/Hotbar.js'

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

// ── Camera state (yaw/pitch are client-side only, not from entity) ────────
let yaw = 0, pitch = -0.3   // slightly downward initial look

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

// ── Keyboard state ────────────────────────────────────────────────────────
const keys = { w: false, a: false, s: false, d: false, space: false, shift: false }

window.addEventListener('keydown', (e) => {
  // Hotbar selection (1-0 keys)
  if (hotbar.handleKeyDown(e)) {
    e.preventDefault()
    return
  }

  switch (e.code) {
    case 'KeyW': case 'ArrowUp':                      keys.w     = true;  e.preventDefault(); break
    case 'KeyA': case 'ArrowLeft':                    keys.a     = true;  e.preventDefault(); break
    case 'KeyS': case 'ArrowDown':                    keys.s     = true;  e.preventDefault(); break
    case 'KeyD': case 'ArrowRight':                   keys.d     = true;  e.preventDefault(); break
    case 'Space':    e.preventDefault(); keys.space = true;  break
    case 'ShiftLeft': case 'ShiftRight': keys.shift = true;  break
    default: return
  }
})
window.addEventListener('keyup', (e) => {
  switch (e.code) {
    case 'KeyW': case 'ArrowUp':                      keys.w     = false; break
    case 'KeyA': case 'ArrowLeft':                    keys.a     = false; break
    case 'KeyS': case 'ArrowDown':                    keys.s     = false; break
    case 'KeyD': case 'ArrowRight':                   keys.d     = false; break
    case 'Space':                        keys.space = false; break
    case 'ShiftLeft': case 'ShiftRight': keys.shift = false; break
    default: return
  }
})

// ── Pointer lock (mouse look) ─────────────────────────────────────────────
renderer.domElement.addEventListener('click', () => {
  renderer.domElement.requestPointerLock()
})

document.addEventListener('mousemove', (e) => {
  if (document.pointerLockElement !== renderer.domElement) return
  yaw   -= e.movementX * 0.002
  pitch -= e.movementY * 0.002
  pitch  = Math.max(-Math.PI / 2 + 0.01, Math.min(Math.PI / 2 - 0.01, pitch))
})

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

/** Compute button bitmask from current key state. @returns {number} */
function computeButtons() {
  let b = 0
  if (keys.w)     b |= InputButton.FORWARD
  if (keys.s)     b |= InputButton.BACKWARD
  if (keys.a)     b |= InputButton.LEFT
  if (keys.d)     b |= InputButton.RIGHT
  if (keys.space) b |= InputButton.JUMP
  if (keys.shift) b |= InputButton.DESCEND
  return b
}

/** Send INPUT frame only when state changed. */
function sendInputIfChanged(buttons, yawVal, pitchVal) {
  const inputType = slotToInputType(hotbar.selectedIndex)
  if (buttons === lastButtons && yawVal === lastYaw && pitchVal === lastPitch && inputType === lastInputType) return
  client.sendInput(NetworkProtocol.serializeInput(inputType, buttons, yawVal, pitchVal))
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

  const buttons = computeButtons()
  sendInputIfChanged(buttons, yaw, pitch)

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
  hud.textContent =
    `[${modeName}]  pos  ${vposX.toFixed(1)}  ${vposY.toFixed(1)}  ${vposZ.toFixed(1)}` +
    `   yaw ${(yaw * 180 / Math.PI).toFixed(0)}°`
}

animate()
