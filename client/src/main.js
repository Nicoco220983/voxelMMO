// @ts-check
import * as THREE      from 'three'
import { GameClient }  from './GameClient.js'
import { ChunkManager } from './ChunkManager.js'

// ── Renderer ──────────────────────────────────────────────────────────────
const renderer = new THREE.WebGLRenderer({ antialias: true })
renderer.setSize(window.innerWidth, window.innerHeight)
renderer.setPixelRatio(devicePixelRatio)
document.body.appendChild(renderer.domElement)

// ── Scene ─────────────────────────────────────────────────────────────────
const scene = new THREE.Scene()
scene.background = new THREE.Color(0x87ceeb)  // sky blue
scene.fog = new THREE.Fog(0x87ceeb, 200, 600)

// ── Camera ────────────────────────────────────────────────────────────────
const camera = new THREE.PerspectiveCamera(
  75, window.innerWidth / window.innerHeight, 0.1, 1000)
camera.position.set(32, 30, 80)
camera.lookAt(32, 16, 32)

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

// ── Chunk manager ─────────────────────────────────────────────────────────
const chunkManager = new ChunkManager(scene)

// ── Network connection ────────────────────────────────────────────────────
const wsUrl = `ws://${location.host}/ws`
const client = new GameClient(wsUrl)

client.onChunkMessage((type, chunkId, view) => {
  chunkManager.handleChunkMessage(type, chunkId, view)
})

client.connect().catch((err) => {
  console.error('[main] Failed to connect to', wsUrl, err)
})

// ── HUD ───────────────────────────────────────────────────────────────────
const hud = /** @type {HTMLElement} */ (document.getElementById('hud'))

// ── Render loop ───────────────────────────────────────────────────────────

/**
 * Main render loop — called by requestAnimationFrame every frame.
 */
function animate() {
  requestAnimationFrame(animate)

  chunkManager.rebuildDirtyChunks()
  renderer.render(scene, camera)

  const p = camera.position
  hud.textContent = `pos  ${p.x.toFixed(1)}  ${p.y.toFixed(1)}  ${p.z.toFixed(1)}`
}

animate()
