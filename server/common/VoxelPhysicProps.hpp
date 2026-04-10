#pragma once
#include "VoxelPhysicType.hpp"
#include <array>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Physical properties for a voxel physics type.
 * 
 * Designed for zero-overhead lookup: 256 * 6 = 1.5KB, fits in L1 cache.
 * No virtual calls, no branching on voxel type.
 * 
 * Key design: velocity is NOT modified every tick. Instead:
 * - maxSpeedXZ/Y: Caps applied on input or landing (rare events)
 * - restitution: Applied only on collision (rare event)
 * This preserves constant velocity for client prediction.
 */
struct VoxelPhysicProps {
    /// Max horizontal speed when on this surface (sub-voxels/tick). 0 = no limit.
    uint16_t maxSpeedXZ;
    
    /// Max vertical speed when in this voxel (sub-voxels/tick). 0 = no limit.
    uint16_t maxSpeedY;
    
    /// Restitution (bounce) on collision: 0-255. Applied only on collision.
    uint8_t restitution;
    
    /// Flags: SOLID, CLIMBABLE, INFLICTS_FALL_DAMAGE, etc.
    uint8_t flags;
    
    static constexpr uint8_t FLAG_SOLID               = 1 << 0;
    static constexpr uint8_t FLAG_CLIMBABLE           = 1 << 1;  ///< Entity can climb (no gravity)
    static constexpr uint8_t FLAG_INFLICTS_FALL_DAMAGE = 1 << 2;  ///< Landing causes fall damage
};

/**
 * @brief Create the static lookup table for voxel physics properties.
 * Defined as a separate function to avoid incomplete type issues.
 */
inline constexpr std::array<VoxelPhysicProps, 256> makeVoxelPhysicPropsTable() {
    std::array<VoxelPhysicProps, 256> table{};
    
    // Initialize all to SOLID-like defaults (FLAG_INFLICTS_FALL_DAMAGE set)
    for (auto& p : table) {
        p = VoxelPhysicProps{0, 0, 0, VoxelPhysicProps::FLAG_SOLID | VoxelPhysicProps::FLAG_INFLICTS_FALL_DAMAGE};
    }
    
    // AIR: non-solid, no fall damage
    table[VoxelPhysicTypes::AIR] = VoxelPhysicProps{0, 0, 0, 0};
    
    // SLIME: bouncy, no speed limits, no fall damage
    table[VoxelPhysicTypes::SLIME] = VoxelPhysicProps{0, 0, 255, VoxelPhysicProps::FLAG_SOLID};
    
    // MUD: slow movement (half normal speed), no fall damage (soft landing)
    // Normal player speed ~77 sub-voxels/tick, mud caps at ~30
    table[VoxelPhysicTypes::MUD] = VoxelPhysicProps{30, 0, 0, VoxelPhysicProps::FLAG_SOLID};
    
    // LADDER: climbable, non-solid, no speed limits, no fall damage
    table[VoxelPhysicTypes::LADDER] = VoxelPhysicProps{0, 0, 0, VoxelPhysicProps::FLAG_CLIMBABLE};
    
    return table;
}

/// Static lookup table - O(1), inline, L1 cache friendly.
inline constexpr std::array<VoxelPhysicProps, 256> VOXEL_PHYSIC_PROPS = makeVoxelPhysicPropsTable();

/**
 * @brief O(1) lookup of physical properties by VoxelPhysicType.
 * Inline, no branching, L1 cache friendly.
 */
inline const VoxelPhysicProps& getVoxelPhysicProps(VoxelPhysicType type) {
    return VOXEL_PHYSIC_PROPS[type];
}

} // namespace voxelmmo
