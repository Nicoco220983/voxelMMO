#pragma once
#include "Types.hpp"

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
} // namespace VoxelTypes

} // namespace voxelmmo
