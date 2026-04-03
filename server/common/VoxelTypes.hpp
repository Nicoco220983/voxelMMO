#pragma once
#include "Types.hpp"
#include "VoxelPhysicType.hpp"
#include <array>

namespace voxelmmo {

/**
 * @brief Named constants for every known VoxelType value.
 *
 * Must stay in sync with client/src/VoxelTypes.js.
 * @note GRASS has been removed. BASIC is now the first solid voxel type (1).
 */
namespace VoxelTypes {
    inline constexpr VoxelType AIR   = 0;
    inline constexpr VoxelType BASIC = 1;
    inline constexpr VoxelType STONE = 2;
    inline constexpr VoxelType DIRT  = 3;
    inline constexpr VoxelType PLANKS  = 4;
    inline constexpr VoxelType BRICKS  = 5;
    inline constexpr VoxelType SLIME = 6;  ///< Bouncy surface
    inline constexpr VoxelType MUD   = 7;  ///< Slow movement
} // namespace VoxelTypes

/**
 * @brief Mapping from VoxelType to VoxelPhysicType.
 * 
 * O(1) lookup table. Size is 256 now (fits uint8), expandable if VoxelType grows.
 * Most voxels map to SOLID physics type.
 */
inline constexpr std::array<VoxelPhysicType, 256> makeVoxelToPhysicTable() {
    std::array<VoxelPhysicType, 256> table{};
    table.fill(VoxelPhysicTypes::SOLID);  // default
    
    table[VoxelTypes::AIR]   = VoxelPhysicTypes::AIR;
    table[VoxelTypes::SLIME] = VoxelPhysicTypes::SLIME;
    table[VoxelTypes::MUD]   = VoxelPhysicTypes::MUD;
    
    return table;
}

inline constexpr std::array<VoxelPhysicType, 256> VOXEL_TO_PHYSIC = makeVoxelToPhysicTable();

/**
 * @brief Get the physics type for a voxel type.
 * O(1), inline, no branching.
 */
inline VoxelPhysicType toVoxelPhysicType(VoxelType vt) {
    return VOXEL_TO_PHYSIC[vt];
}

} // namespace voxelmmo
