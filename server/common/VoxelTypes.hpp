#pragma once
#include "Types.hpp"

namespace voxelmmo {

/**
 * @brief Named constants for every known VoxelType value.
 *
 * Must stay in sync with client/src/types.js VoxelType enum.
 */
namespace VoxelTypes {
    inline constexpr VoxelType AIR   = 0;
    inline constexpr VoxelType STONE = 1;
    inline constexpr VoxelType DIRT  = 2;
    inline constexpr VoxelType GRASS = 3;  // deprecated
    inline constexpr VoxelType BASIC = 4;
} // namespace VoxelTypes

} // namespace voxelmmo
