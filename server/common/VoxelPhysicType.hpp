#pragma once
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Physics behavior type for voxels - uint8 fixed forever.
 * 
 * VoxelType may grow beyond uint8 in the future, but VoxelPhysicType
 * stays uint8 to keep physics lookups fast and cache-friendly.
 * Multiple VoxelTypes can map to the same VoxelPhysicType.
 */
using VoxelPhysicType = uint8_t;

namespace VoxelPhysicTypes {
    constexpr VoxelPhysicType AIR    = 0;  ///< Non-solid, no physics effects
    constexpr VoxelPhysicType SOLID  = 1;  ///< Default solid block
    constexpr VoxelPhysicType SLIME  = 2;  ///< Bouncy surface
    constexpr VoxelPhysicType MUD    = 3;  ///< Slow movement (speed cap)
    constexpr VoxelPhysicType LADDER = 4;  ///< Climbable, no gravity when inside
    // Reserved: 5-255 for future physics types
} // namespace VoxelPhysicTypes

} // namespace voxelmmo
