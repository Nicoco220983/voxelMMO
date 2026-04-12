// @ts-check
import { RenderManager } from './RenderManager.js'
import { GameClient } from './GameClient.js'
import { SUBVOXEL_SIZE, TICK_RATE } from './types.js'
import { EntityType } from './EntityCatalog.js'
import { Hotbar } from './ui/Hotbar.js'
import { VoxelHighlight } from './ui/VoxelHighlight.js'
import { BulkVoxelsSelection } from './ui/BulkVoxelsSelection.js'
import { ToolContext } from './ui/ToolContext.js'
import { DeathScreen } from './ui/DeathScreen.js'
import { createController } from './controllers/ControllerManager.js'
import { voxelTexturesReady } from './VoxelTextures.js'

/** @typedef {import('./types.js').SubVoxelCoord} SubVoxelCoord */

// ── Texture Loading Guard ───────────────────────────────────────────────────
let texturesReady = false
voxelTexturesReady.then(() => {
  texturesReady = true
  console.info('[main] Voxel textures loaded')
})

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

// ── Death Screen ────────────────────────────────────────────────────────────
const deathScreen = new DeathScreen(() => {
  // Respawn callback: hide screen and send new join
  deathScreen.hide()
  client.clear()  // Clear local entities
  client.sendJoin(_entityType, _sessionToken)
  console.info('[main] Respawn JOIN sent')
})

// Set up death detection callback
client.onDeath(() => {
  console.info('[main] Player died, showing death screen')
  deathScreen.show()
})



client.connect().then(() => {
  client.sendJoin(_entityType, _sessionToken)
}).catch((err) => {
  console.error('[main] Failed to connect to server', err)
})

// ── Voxel Highlight System ────────────────────────────────────────────────
const voxelHighlight = new VoxelHighlight(scene)

// ── Bulk Voxels Selection ─────────────────────────────────────────────────
const bulkSelection = new BulkVoxelsSelection(scene)

// ── Tool Context (centralized tool state) ─────────────────────────────────
const toolContext = new ToolContext({ voxelHighlight, bulkSelection })

// ── Hotbar ─────────────────────────────────────────────────────────────────
const hotbar = new Hotbar({ toolContext, chunkRegistry: client.chunkRegistry })

// Handle tool unselection (ESC/BACK pressed)
hotbar.onToolUnselected = () => {
  // ToolContext.currentTool is updated by Hotbar
}

// ── Controller (keyboard or touch) ────────────────────────────────────────
const controller = createController(renderer.domElement, { toolContext, hotbar })

// Set game client reference for controller to access player entity
controller.setGameClient(client)

// ── HUD ───────────────────────────────────────────────────────────────────
const hud = /** @type {HTMLElement} */ (document.getElementById('hud'))
const damageFlash = /** @type {HTMLElement} */ (document.getElementById('damage-flash'))

/** Show damage flash effect */
function showDamageFlash() {
  if (!damageFlash) return
  damageFlash.classList.add('active')
  // Remove after animation completes
  setTimeout(() => {
    damageFlash.classList.remove('active')
  }, 300)
}

// ── FPS Tracking ───────────────────────────────────────────────────────────
const fpsTracker = {
  frameCount: 0,
  lastFpsTime: performance.now(),
  currentFps: 0,

  /** Update FPS tracking, returns current FPS */
  update() {
    this.frameCount++
    const now = performance.now()
    const fpsDt = now - this.lastFpsTime
    if (fpsDt >= 1000) {
      this.currentFps = Math.round((this.frameCount * 1000) / fpsDt)
      this.frameCount = 0
      this.lastFpsTime = now
    }
    return this.currentFps
  },
}

// ── Render loop ───────────────────────────────────────────────────────────
let lastTime = performance.now()

function animate() {
  requestAnimationFrame(animate)

  const now = performance.now()
  const dt  = Math.min((now - lastTime) / 1000, 0.1)  // cap at 100 ms
  lastTime  = now

  // ── Controller update (compute button masks, movement deltas) ───────────
  controller.update(dt)

  // ── Comprehensive controller sync ───────────────────────────────────────
  // Handles: tool unselection, hotbar selection, pending inputs, targeting,
  // voxel highlighting, bulk selection, input sending, and camera sync
  const posInfo = controller.sync(voxelHighlight, client.chunkRegistry, camera, dt)

  // ── Entity updates ──────────────────────────────────────────────────────
  // Update entity animations (sheep leg swing, player mesh position, etc.)
  client.updateEntities(dt)

  client.pruneDistantChunks(posInfo.vposX, posInfo.vposZ)
  if (texturesReady) client.rebuildDirtyChunks()

  composer.render()

  // ── FPS calculation ─────────────────────────────────────────────────────
  const currentFps = fpsTracker.update()

  // ── HUD Update ──────────────────────────────────────────────────────────
  // Get player health for display
  const selfEntity = client.selfEntity
  const healthText = selfEntity?.health 
    ? `hp ${selfEntity.health.current}/${selfEntity.health.max}  ` 
    : ''
  
  // Check for damage using hasBeenDamaged() (resets after check)
  if (selfEntity?.health?.hasBeenDamaged()) {
    showDamageFlash()
  }
  
  hud.textContent =
    `${healthText}pos  ${posInfo.vposX.toFixed(1)}  ${posInfo.vposY.toFixed(1)}  ${posInfo.vposZ.toFixed(1)}   fps ${currentFps}`

  // ── Reset controller frame state ────────────────────────────────────────
  // Resets one-shot flags (toolActivated, selectedSlotIndex, etc.)
  controller.resetFrameState(dt)
}

animate()
