#pragma once
#include "common/VoxelPhysicType.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

/**
 * @brief Tracks what voxel physics type the entity is in contact with.
 * 
 * Updated by PhysicsSystem each tick. Used by InputSystem to apply
 * surface-specific speed caps (e.g., mud = slower movement).
 * 
 * This avoids querying chunk data in InputSystem (which doesn't have
 * access to ChunkRegistry) and keeps the physics surface detection
 * logic in one place.
 */
struct GroundContactComponent {
    VoxelPhysicType groundType = VoxelPhysicTypes::AIR;  ///< Surface entity is standing on
    VoxelPhysicType wallTypeX = VoxelPhysicTypes::AIR;   ///< Wall hit in X direction (if any)
    VoxelPhysicType wallTypeZ = VoxelPhysicTypes::AIR;   ///< Wall hit in Z direction (if any)
    
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
};

} // namespace voxelmmo
