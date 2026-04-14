// @ts-check

/**
 * @typedef {Object} GameStateType
 * @property {boolean} debugMode - Whether debug visualizations are enabled.
 */

/**
 * Global mutable game state singleton.
 * Imported by any module that needs access to global client settings.
 * @type {GameStateType}
 */
export const GameState = {
  debugMode: false,
}
