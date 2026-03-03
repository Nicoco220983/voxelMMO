// @ts-check
import * as THREE     from 'three'
import { GameClient } from './GameClient.js'
import {
  SUBVOXEL_SIZE, TICK_RATE, EntityType, ClientMessageType, InputButton,
  GHOST_MOVE_SPEED_VOXELS, PLAYER_WALK_SPEED_VOXELS, PLAYER_JUMP_VY_VOXELS,
  GRAVITY_DECREMENT,
} from './types.js'

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
client.connect().then(() => {
  client.sendJoin(_entityType)
}).catch((err) => {
  console.error('[main] Failed to connect to server', err)
})

// ── Local player state ────────────────────────────────────────────────────
// Approximate spawn position for client-side prediction; server corrects via deltas.
// Server computes exact Y = surfaceY(32, 32) + 2; surface ∈ [4, 30] so Y ∈ [6, 32].
let posX = 32 * SUBVOXEL_SIZE   // 8192
let posY = 22 * SUBVOXEL_SIZE   // 5632  (approx surfaceY + 2)
let posZ = 32 * SUBVOXEL_SIZE   // 8192
let yaw = 0, pitch = -0.3   // slightly downward initial look
let predVy = 0              // local predicted Y velocity (sub-voxels/tick), PLAYER only
let predGrounded = false    // local predicted grounded state

// ── Keyboard state ────────────────────────────────────────────────────────
const keys = { w: false, a: false, s: false, d: false, space: false, shift: false }

window.addEventListener('keydown', (e) => {
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
const _inputBuf  = new ArrayBuffer(10)
const _inputView = new DataView(_inputBuf)
_inputView.setUint8(0, ClientMessageType.INPUT)   // type prefix, set once

/** @type {number} */ let lastButtons = -1  // force first send (NaN-like)
/** @type {number} */ let lastYaw     = NaN
/** @type {number} */ let lastPitch   = NaN

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
  if (buttons === lastButtons && yawVal === lastYaw && pitchVal === lastPitch) return
  _inputView.setUint8(1, buttons)
  _inputView.setFloat32(2, yawVal,   true)
  _inputView.setFloat32(6, pitchVal, true)
  client.sendInput(_inputBuf)
  lastButtons = buttons; lastYaw = yawVal; lastPitch = pitchVal
}

// ── HUD ───────────────────────────────────────────────────────────────────
const hud = /** @type {HTMLElement} */ (document.getElementById('hud'))

// ── Render loop ───────────────────────────────────────────────────────────
const TICK_DT = 1 / TICK_RATE

let lastTime = performance.now()

function animate() {
  requestAnimationFrame(animate)

  const now = performance.now()
  const dt  = Math.min((now - lastTime) / 1000, 0.1)  // cap at 100 ms
  lastTime  = now

  const buttons = computeButtons()
  sendInputIfChanged(buttons, yaw, pitch)

  // ── Client-side prediction (visual; server deltas are authoritative) ──
  if (_entityType === EntityType.GHOST_PLAYER) {
    // 3-D flight: mirror server InputSystem GHOST logic
    const cy = Math.cos(yaw),   sy = Math.sin(yaw)
    const cp = Math.cos(pitch),  sp = Math.sin(pitch)
    let dx = 0, dy = 0, dz = 0
    if (buttons & InputButton.FORWARD)  { dx += -sy*cp; dy += sp; dz += -cy*cp }
    if (buttons & InputButton.BACKWARD) { dx -= -sy*cp; dy -= sp; dz -= -cy*cp }
    if (buttons & InputButton.LEFT)     { dx -= cy;               dz -= -sy     }
    if (buttons & InputButton.RIGHT)    { dx += cy;               dz += -sy     }
    if (buttons & InputButton.JUMP)     { dy += 1 }
    if (buttons & InputButton.DESCEND)  { dy -= 1 }
    const len = Math.sqrt(dx*dx + dy*dy + dz*dz)
    const s   = len > 0.001 ? GHOST_MOVE_SPEED_VOXELS / len : 0
    posX += dx * s * SUBVOXEL_SIZE * dt
    posY += dy * s * SUBVOXEL_SIZE * dt
    posZ += dz * s * SUBVOXEL_SIZE * dt

  } else {
    // Horizontal-only movement; Y uses approximate local gravity (no collision)
    const cy = Math.cos(yaw), sy = Math.sin(yaw)
    let dx = 0, dz = 0
    if (buttons & InputButton.FORWARD)  { dx += -sy; dz += -cy }
    if (buttons & InputButton.BACKWARD) { dx -= -sy; dz -= -cy }
    if (buttons & InputButton.LEFT)     { dx -= cy;  dz -= -sy  }
    if (buttons & InputButton.RIGHT)    { dx += cy;  dz += -sy  }
    const hlen = Math.sqrt(dx*dx + dz*dz)
    const hs   = hlen > 0.001 ? PLAYER_WALK_SPEED_VOXELS / hlen : 0
    posX += dx * hs * SUBVOXEL_SIZE * dt
    posZ += dz * hs * SUBVOXEL_SIZE * dt

    // Jump impulse
    if ((buttons & InputButton.JUMP) && predGrounded) {
      predVy = PLAYER_JUMP_VY_VOXELS * SUBVOXEL_SIZE / TICK_RATE
      predGrounded = false
    }

    // Apply local gravity (approximate; server corrects via authoritative deltas)
    predVy = Math.max(predVy - GRAVITY_DECREMENT, -128 * SUBVOXEL_SIZE / TICK_RATE)
    posY  += predVy * dt

    // Approximate ground clamp (no collision data on client)
    if (posY < 8 * SUBVOXEL_SIZE) {
      posY = 8 * SUBVOXEL_SIZE
      predVy = 0
      predGrounded = true
    }
  }

  // Divide by SUBVOXEL_SIZE to get voxel coordinates for Three.js.
  camera.position.set(posX / SUBVOXEL_SIZE, posY / SUBVOXEL_SIZE, posZ / SUBVOXEL_SIZE)
  camera.rotation.y = yaw
  camera.rotation.x = pitch

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
