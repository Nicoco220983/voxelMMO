#pragma once
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Server-only physics behaviour selector. NOT serialised to wire.
 *
 * GHOST  – No gravity, no collision. Advances by velocity only.
 *           Outputs grounded=true (suppresses client gravity prediction).
 *           Used for all current player-controlled entities.
 *
 * FLYING – No gravity, WITH swept AABB collision on all three axes.
 *           Outputs grounded=false. For future flying mobs.
 *
 * FULL   – Gravity applied every tick + swept AABB collision.
 *           grounded output set by sweepY each tick.
 *           For future NPCs, falling items, walking mobs.
 */
enum class PhysicsMode : uint8_t { GHOST = 0, FLYING = 1, FULL = 2 };

struct PhysicsModeComponent {
    PhysicsMode mode{PhysicsMode::GHOST};
};

} // namespace voxelmmo
