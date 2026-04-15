// @ts-check

/**
 * @typedef {import('./tools/Tool.js').Tool} Tool
 * @typedef {typeof import('./tools/Tool.js').Tool} ToolClass
 */

/**
 * Tool type enumeration.
 * Must match server ToolType enum.
 */
export const ToolType = {
  HAND: 0,
  VOXEL: 1,
  NONE: 255,
}

/**
 * ToolCatalog maps tool IDs to Tool CLASSES.
 * Each Tool class contains static constants (damage, cooldown, icon, etc.)
 * and can create instances for behavior/input handling.
 * 
 * Note: Tools must be registered via registerTool() before use.
 * This avoids circular dependencies (ToolCatalog doesn't import Tool classes).
 */
export const ToolCatalog = {}

/**
 * Register a Tool class for a tool ID.
 * Call this at initialization time (e.g., in main.js).
 * @param {number} toolId
 * @param {ToolClass} ToolClass
 */
export function registerTool(toolId, ToolClass) {
  ToolCatalog[toolId] = ToolClass
}

/**
 * Get the Tool CLASS for a tool ID.
 * Use this to access static constants or create instances.
 * @param {number} toolId
 * @returns {ToolClass|null}
 */
export function getToolClass(toolId) {
  return ToolCatalog[toolId] || null
}

/**
 * Get tool metadata for UI display.
 * Returns plain object with tool info (no class reference).
 * @param {number} toolId
 * @returns {{id: number, name: string, icon: string, damage: number, cooldownTicks: number, range: number}|null}
 */
export function getToolInfo(toolId) {
  const ToolClass = getToolClass(toolId)
  if (!ToolClass) return null
  
  return {
    id: toolId,
    name: ToolClass.NAME || ToolClass.name,
    icon: ToolClass.ICON || '❓',
    damage: ToolClass.DAMAGE || 0,
    cooldownTicks: ToolClass.COOLDOWN_TICKS || 0,
    range: ToolClass.RANGE || 0,
  }
}

/**
 * Create a tool instance from a tool ID.
 * @param {number} toolId
 * @param {...any} args - Constructor arguments
 * @returns {Tool|null}
 */
export function createToolInstance(toolId, ...args) {
  const ToolClass = getToolClass(toolId)
  return ToolClass ? new ToolClass(...args) : null
}

/**
 * Get cooldown in milliseconds for a tool.
 * @param {number} toolId
 * @returns {number}
 */
export function getCooldownMs(toolId) {
  const ToolClass = getToolClass(toolId)
  if (!ToolClass || !ToolClass.COOLDOWN_TICKS) return 0
  // 20 ticks per second = 50ms per tick
  return ToolClass.COOLDOWN_TICKS * 50
}
