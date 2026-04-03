#include "game/components/GroundContactComponent.hpp"
#include "common/VoxelPhysicProps.hpp"

namespace voxelmmo {

uint16_t GroundContactComponent::getMaxSpeedXZ() const {
    if (groundType == VoxelPhysicTypes::AIR) return 0;
    return voxelmmo::getVoxelPhysicProps(groundType).maxSpeedXZ;
}

uint8_t GroundContactComponent::getRestitution() const {
    if (groundType == VoxelPhysicTypes::AIR) return 0;
    return voxelmmo::getVoxelPhysicProps(groundType).restitution;
}

} // namespace voxelmmo
