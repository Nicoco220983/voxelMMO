// @ts-check

/** @typedef {'low'|'medium'|'high'|'ultra'} GraphicsPreset */

/**
 * @typedef {Object} GameContextType
 * @property {boolean} debugMode - Whether debug visualizations are enabled.
 * @property {boolean} isMobile - Whether the client is running on a mobile device.
 * @property {GraphicsPreset} graphicsPreset - Current graphics quality preset.
 * @property {number} pixelRatio - Renderer pixel ratio cap.
 * @property {boolean} ssaoEnabled - Screen-space ambient occlusion.
 * @property {boolean} shadowsEnabled - Dynamic shadows.
 * @property {number} chunkLoadRadius - Max chunk radius from player.
 * @property {number} fogDensity - Fog density for distance culling.
 */

/** @type {Record<GraphicsPreset, {pixelRatio:number, ssaoEnabled:boolean, shadowsEnabled:boolean, chunkLoadRadius:number, fogDensity:number}>} */
export const GRAPHICS_PRESETS = {
  low:    { pixelRatio: 1,   ssaoEnabled: false, shadowsEnabled: false, chunkLoadRadius: 6,  fogDensity: 0.035 },
  medium: { pixelRatio: 1.5, ssaoEnabled: false, shadowsEnabled: true,  chunkLoadRadius: 8,  fogDensity: 0.025 },
  high:   { pixelRatio: 2,   ssaoEnabled: true,  shadowsEnabled: true,  chunkLoadRadius: 10, fogDensity: 0.02 },
  ultra:  { pixelRatio: 3,   ssaoEnabled: true,  shadowsEnabled: true,  chunkLoadRadius: 14, fogDensity: 0.015 },
}

/**
 * Apply a graphics preset to the game context.
 * @param {GameContextType} ctx
 * @param {GraphicsPreset} preset
 */
export function applyGraphicsPreset(ctx, preset) {
  const settings = GRAPHICS_PRESETS[preset]
  ctx.graphicsPreset = preset
  ctx.pixelRatio = settings.pixelRatio
  ctx.ssaoEnabled = settings.ssaoEnabled
  ctx.shadowsEnabled = settings.shadowsEnabled
  ctx.chunkLoadRadius = settings.chunkLoadRadius
  ctx.fogDensity = settings.fogDensity
}

/**
 * Global mutable game context singleton.
 * Imported by any module that needs access to global client settings.
 * @type {GameContextType}
 */
export const GameContext = {
  debugMode: false,
  isMobile: false,
  graphicsPreset: 'high',
  pixelRatio: 2,
  ssaoEnabled: true,
  shadowsEnabled: true,
  chunkLoadRadius: 10,
  fogDensity: 0.02,
}
