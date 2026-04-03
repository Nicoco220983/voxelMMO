#pragma once
#include "common/VoxelPhysicType.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

/**
 * @brief Tracks what voxel physics type the entity is in contact with.
 * 
 * Updated by PhysicsSystem each tick. Used by:
 * - InputSystem: to apply surface-specific speed caps (e.g., mud = slower movement)
 * - JumpSystem: to detect if entity can jump and handle bounce+jump
 * 
 * This avoids querying chunk data in systems that don't have access to ChunkRegistry
 * and keeps the physics surface detection logic in one place.
 */
struct GroundContactComponent {
    VoxelPhysicType groundType = VoxelPhysicTypes::AIR;  ///< Surface entity is standing on
    VoxelPhysicType wallTypeX = VoxelPhysicTypes::AIR;   ///< Wall hit in X direction (if any)
    VoxelPhysicType wallTypeZ = VoxelPhysicTypes::AIR;   ///< Wall hit in Z direction (if any)
    
    /// True if entity transitioned from airborne to grounded this tick
    bool justLanded = false;
    
    /// Bounce velocity applied this tick (0 if no bounce). Used by JumpSystem to add jump boost.
    int32_t bounceVelocity = 0;
    
    /// True when entity center is inside a climbable voxel (ladder)
    bool isClimbing = false;
    
    /**
     * @brief Helper to get the max speed cap for the current ground surface.
     * @return Max speed in sub-voxels/tick, or 0 if no limit.
     */
    uint16_t getMaxSpeedXZ() const;
    
    /**
     * @brief Helper to get restitution (bounce) for the current ground surface.
     * @return Restitution 0-255.
     */
    uint8_t getRestitution() const;
    
    /**
     * @brief Check if entity is currently grounded (standing on a surface).
     * @return true if on ground
     */
    bool isGrounded() const {
        return groundType != VoxelPhysicTypes::AIR;
    }
    
    /**
     * @brief Check if a bounce was applied this tick.
     * @return true if bouncing
     */
    bool isBouncing() const {
        return bounceVelocity != 0;
    }
};

} // namespace voxelmmo
