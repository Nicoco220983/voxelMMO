#pragma once
#include "common/Types.hpp"
#include <cstdint>
#include <cmath>

namespace voxelmmo {

// Forward declarations
struct InputComponent;

/**
 * @brief Manages entity walking/movement parameters and velocity computation.
 * 
 * This component encapsulates horizontal movement logic for entities that
 * respond to directional input (WASD). It computes velocity based on:
 * - Input buttons (forward/back/left/right)
 * - Facing direction (yaw)
 * - Base walk speed
 * - Optional speed modifiers (sprint, status effects, etc.)
 * 
 * Similar to JumpComponent, this keeps movement logic in the component
 * rather than scattered across entity-specific namespaces.
 */
struct WalkComponent {
    /// Base horizontal speed (sub-voxels per tick)
    int32_t baseSpeed;
    
    /// Current speed multiplier (256 = 1.0x, 512 = 2.0x for sprint)
    uint16_t speedMultiplier = 256;
    
    /// Constructor with default player walk speed
    explicit WalkComponent(int32_t speed = PLAYER_WALK_SPEED) : baseSpeed(speed) {}
    
    /// Default player walk speed: 6 vox/s × SUBVOXEL_SIZE × TICK_DT
    static constexpr int32_t PLAYER_WALK_SPEED = 77;
    
    /// Ghost/fly speed for comparison
    static constexpr int32_t GHOST_SPEED = 256;
    
    /**
     * @brief Get effective speed with all modifiers applied.
     * @return Effective speed in sub-voxels per tick
     */
    int32_t getEffectiveSpeed() const {
        return (baseSpeed * speedMultiplier) / 256;
    }
    
    /**
     * @brief Compute horizontal velocity from input and facing direction.
     * 
     * This is the main entry point for walk logic. Call this to convert
     * input buttons + yaw into velocity changes.
     * 
     * @param input     InputComponent with button states and yaw
     * @param outVx     Output X velocity (modified)
     * @param outVz     Output Z velocity (modified)
     * @param maxSpeed  Optional override for max speed (0 = use effective speed)
     */
    void computeVelocity(const InputComponent& input, 
                         int32_t& outVx, 
                         int32_t& outVz,
                         uint16_t maxSpeed = 0) const;
    
    /**
     * @brief Check if any movement button is pressed.
     * @param input InputComponent with button states
     * @return true if walking
     */
    static bool isWalking(const InputComponent& input);
    
    /**
     * @brief Apply a temporary speed modifier.
     * @param multiplier Speed multiplier in 1/256ths (256 = 1.0x)
     */
    void setSpeedMultiplier(uint16_t multiplier) {
        speedMultiplier = multiplier;
    }
    
    /**
     * @brief Reset speed multiplier to normal (1.0x).
     */
    void resetSpeedMultiplier() {
        speedMultiplier = 256;
    }
    
    /**
     * @brief Check if currently sprinting (multiplier > 1.0x).
     */
    bool isSprinting() const {
        return speedMultiplier > 256;
    }
};

} // namespace voxelmmo
