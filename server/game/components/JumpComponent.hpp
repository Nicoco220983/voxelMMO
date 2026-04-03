#pragma once
#include "common/Types.hpp"
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Manages player jump state for auto-jump.
 * 
 * Features:
 * - Auto-jump when holding jump button and landing (with cooldown)
 * - When bouncing on slime while holding jump: bounce + jump combined
 * 
 * This component encapsulates all jump logic. Systems should call tryAutoJump()
 * rather than manually checking cooldowns and updating state.
 */
struct JumpComponent {
    /// Tick of last jump (for cooldown tracking)
    uint32_t lastJumpTick = 0;
    
    /// Minimum ticks between jumps (5 ticks at 20 TPS = 250ms)
    static constexpr uint32_t MIN_JUMP_INTERVAL_TICKS = 5;
    
    /// Player is holding jump button
    bool wantsToJump = false;
    
    /**
     * @brief Check if jump cooldown has expired.
     * @param currentTick Current game tick
     * @return true if can jump again
     */
    bool canJump(uint32_t currentTick) const {
        return (currentTick - lastJumpTick) >= MIN_JUMP_INTERVAL_TICKS;
    }
    
    /**
     * @brief Record that a jump was performed.
     * @param currentTick Current game tick
     */
    void onJumpExecuted(uint32_t currentTick) {
        lastJumpTick = currentTick;
    }
    
    /**
     * @brief Try to execute an auto-jump if conditions are met.
     * 
     * This is the main entry point for jump logic. Call this when the entity
     * is on ground and might want to jump (e.g., after landing or when holding
     * jump button on the ground).
     * 
     * @param currentTick Current game tick
     * @param grounded    Whether entity is on ground
     * @param currentVy   Current vertical velocity
     * @param jumpVy      Jump impulse velocity (e.g., PlayerEntity::PLAYER_JUMP_VY)
     * @return int32_t    New vertical velocity (jumpVy if jumped, currentVy otherwise)
     */
    int32_t tryAutoJump(uint32_t currentTick, bool grounded, int32_t currentVy, int32_t jumpVy) {
        // Can only jump if grounded, holding jump button, cooldown expired, and not already moving up
        if (!grounded || !wantsToJump || !canJump(currentTick) || currentVy != 0) {
            return currentVy;
        }
        
        onJumpExecuted(currentTick);
        return jumpVy;
    }
    
    /**
     * @brief Try to execute a jump with bounce (for slime blocks).
     * 
     * This combines a bounce velocity with a jump boost when the player is
     * holding the jump button. Unlike regular auto-jump, this ignores cooldown
     * to ensure responsive bounce-jumping.
     * 
     * @param currentTick Current game tick
     * @param bounceVy    Base bounce velocity from surface restitution
     * @param jumpVy      Jump impulse velocity to add (e.g., PlayerEntity::PLAYER_JUMP_VY)
     * @param minBounceThreshold Minimum bounce velocity to trigger bounce
     * @return int32_t    New vertical velocity (bounce + jump if conditions met, bounceVy otherwise)
     */
    int32_t tryBounceJump(uint32_t currentTick, int32_t bounceVy, int32_t jumpVy, int32_t minBounceThreshold) {
        // Only bounce if bounce velocity is significant
        if (std::abs(bounceVy) < minBounceThreshold) {
            return 0; // No bounce
        }
        
        // If holding jump, add jump boost to bounce (no cooldown for slime bounces)
        if (wantsToJump) {
            onJumpExecuted(currentTick);
            return bounceVy + jumpVy;
        }
        
        return bounceVy;
    }
};

} // namespace voxelmmo
