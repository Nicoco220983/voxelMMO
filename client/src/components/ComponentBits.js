// @ts-check
/**
 * @file ComponentBits.js
 * @description Component dirty-bit constants for entity serialization.
 * Must stay in sync with server component headers.
 *
 * These bits are used in the component_mask field of entity messages
 * to indicate which components are present in the serialized data.
 */

/** Position component bit - indicates DynamicPositionComponent data present */
export const POSITION_BIT = 1 << 0

/** AI behavior component bit - shared by sheep, goblin, etc. */
export const AI_BEHAVIOR_BIT = 1 << 1

/** @deprecated Use AI_BEHAVIOR_BIT instead */
export const SHEEP_BEHAVIOR_BIT = AI_BEHAVIOR_BIT

/** @deprecated Use AI_BEHAVIOR_BIT instead */
export const GOBLIN_BEHAVIOR_BIT = AI_BEHAVIOR_BIT

/** Health component bit - indicates HealthComponent data present */
export const HEALTH_BIT = 1 << 2

/** Tool component bit - indicates ToolComponent data present (player/ghost only) */
export const TOOL_BIT = 1 << 3
