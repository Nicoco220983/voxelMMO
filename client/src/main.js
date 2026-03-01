// @ts-check
import * as THREE     from 'three'
import { GameClient } from './GameClient.js'
import { SUBVOXEL_SIZE, TICK_RATE } from './types.js'

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
const client = new GameClient(`ws://${location.host}/ws`, scene)
client.connect().catch((err) => {
  console.error('[main] Failed to connect to server', err)
})

// ── Ghost player state ────────────────────────────────────────────────────
// Spawn position in sub-voxels — must match server addPlayer() call in main.cpp
let posX = 32 * SUBVOXEL_SIZE   // 8192
let posY = 20 * SUBVOXEL_SIZE   // 5120
let posZ = 32 * SUBVOXEL_SIZE   // 8192
let yaw = 0, pitch = -0.3   // slightly downward initial look

// ── Speed ramp ────────────────────────────────────────────────────────────
const BASE_SPEED = 10.0   // m/s at rest
const SPEED_STEP = 10.0   // m/s gained per second of continuous movement
const MAX_SPEED  = 100.0  // m/s ceiling

/** @type {number|null} performance.now() timestamp when movement keys first pressed */
let keyHoldStart = null

// ── Keyboard state ────────────────────────────────────────────────────────
const keys = { w: false, a: false, s: false, d: false, space: false, shift: false }

function isMoving() {
  return keys.w || keys.a || keys.s || keys.d || keys.space || keys.shift
}

window.addEventListener('keydown', (e) => {
  const wasMoving = isMoving()
  switch (e.code) {
    case 'ArrowUp':                      keys.w     = true;  e.preventDefault(); break
    case 'ArrowLeft':                    keys.a     = true;  e.preventDefault(); break
    case 'ArrowDown':                    keys.s     = true;  e.preventDefault(); break
    case 'ArrowRight':                   keys.d     = true;  e.preventDefault(); break
    case 'Space':    e.preventDefault(); keys.space = true;  break
    case 'ShiftLeft': case 'ShiftRight': keys.shift = true;  break
    default: return
  }
  if (!wasMoving) keyHoldStart = performance.now()
})
window.addEventListener('keyup', (e) => {
  switch (e.code) {
    case 'ArrowUp':                      keys.w     = false; break
    case 'ArrowLeft':                    keys.a     = false; break
    case 'ArrowDown':                    keys.s     = false; break
    case 'ArrowRight':                   keys.d     = false; break
    case 'Space':                        keys.space = false; break
    case 'ShiftLeft': case 'ShiftRight': keys.shift = false; break
    default: return
  }
  if (!isMoving()) keyHoldStart = null
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

// ── Velocity helper ───────────────────────────────────────────────────────
/**
 * Compute desired velocity from current key state and camera orientation.
 * W/S follow the full 3D camera direction (including pitch).
 * A/D strafe horizontally (yaw only).
 * Space/Shift move straight up/down in world space.
 * @param {number} speed  Current speed magnitude in voxels/s (sent as float to server).
 * @returns {{vx:number, vy:number, vz:number}}  Velocity in voxels/s.
 */
function computeVelocity(speed) {
  // Camera forward in world space (pitch + yaw)
  const fwdX = -Math.sin(yaw) * Math.cos(pitch)
  const fwdY =  Math.sin(pitch)
  const fwdZ = -Math.cos(yaw) * Math.cos(pitch)
  // Right vector (horizontal only, derived from yaw)
  const rgtX =  Math.cos(yaw)
  const rgtZ = -Math.sin(yaw)

  let vx = 0, vy = 0, vz = 0
  if (keys.w)     { vx += fwdX; vy += fwdY; vz += fwdZ }
  if (keys.s)     { vx -= fwdX; vy -= fwdY; vz -= fwdZ }
  if (keys.a)     { vx -= rgtX;              vz -= rgtZ  }
  if (keys.d)     { vx += rgtX;              vz += rgtZ  }
  if (keys.space) { vy += 1 }
  if (keys.shift) { vy -= 1 }

  const len = Math.sqrt(vx * vx + vy * vy + vz * vz)
  if (len > 1) { vx /= len; vy /= len; vz /= len }
  return { vx: vx * speed, vy: vy * speed, vz: vz * speed }
}

// ── Input sending ─────────────────────────────────────────────────────────
const _inputBuf  = new ArrayBuffer(12)
const _inputView = new DataView(_inputBuf)

/** @param {number} vx @param {number} vy @param {number} vz */
function sendVelocity(vx, vy, vz) {
  _inputView.setFloat32(0, vx, true)
  _inputView.setFloat32(4, vy, true)
  _inputView.setFloat32(8, vz, true)
  console.debug('[input] tx velocity', vx.toFixed(2), vy.toFixed(2), vz.toFixed(2))
  client.sendInput(_inputBuf)
}

// ── HUD ───────────────────────────────────────────────────────────────────
const hud = /** @type {HTMLElement} */ (document.getElementById('hud'))

// ── Render loop ───────────────────────────────────────────────────────────
let lastTime = performance.now()
let lastVx = NaN, lastVy = NaN, lastVz = NaN  // NaN forces first send

function animate() {
  requestAnimationFrame(animate)

  const now = performance.now()
  const dt  = Math.min((now - lastTime) / 1000, 0.1)  // cap at 100 ms
  lastTime  = now

  const holdSecs = keyHoldStart !== null ? (now - keyHoldStart) / 1000 : 0
  const currentSpeed = Math.min(BASE_SPEED + Math.floor(holdSecs) * SPEED_STEP, MAX_SPEED)

  const { vx, vy, vz } = computeVelocity(currentSpeed)

  // Send velocity to server only when it changes (voxels/s as float32)
  if (vx !== lastVx || vy !== lastVy || vz !== lastVz) {
    sendVelocity(vx, vy, vz)
    lastVx = vx; lastVy = vy; lastVz = vz
  }

  // Integrate position locally in sub-voxels (client-side prediction).
  // vx/vy/vz are voxels/s; multiply by SUBVOXEL_SIZE × dt to get sub-voxels.
  posX += vx * SUBVOXEL_SIZE * dt
  posY += vy * SUBVOXEL_SIZE * dt
  posZ += vz * SUBVOXEL_SIZE * dt

  // Divide by SUBVOXEL_SIZE to get voxel coordinates for Three.js.
  camera.position.set(posX / SUBVOXEL_SIZE, posY / SUBVOXEL_SIZE, posZ / SUBVOXEL_SIZE)
  camera.rotation.y = yaw
  camera.rotation.x = pitch

  client.pruneDistantChunks(posX / SUBVOXEL_SIZE, posZ / SUBVOXEL_SIZE)
  client.rebuildDirtyChunks()
  renderer.render(scene, camera)

  const vposX = posX / SUBVOXEL_SIZE, vposY = posY / SUBVOXEL_SIZE, vposZ = posZ / SUBVOXEL_SIZE
  hud.textContent =
    `pos  ${vposX.toFixed(1)}  ${vposY.toFixed(1)}  ${vposZ.toFixed(1)}` +
    `   yaw ${(yaw * 180 / Math.PI).toFixed(0)}°` +
    `   spd ${currentSpeed.toFixed(0)}`
}

animate()
