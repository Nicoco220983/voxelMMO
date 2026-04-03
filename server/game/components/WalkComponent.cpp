#include "game/components/WalkComponent.hpp"
#include "game/components/InputComponent.hpp"
#include "common/NetworkProtocol.hpp"
#include <cmath>

namespace voxelmmo {

void WalkComponent::computeVelocity(const InputComponent& input, 
                                    int32_t& outVx, 
                                    int32_t& outVz,
                                    uint16_t maxSpeed) const {
    const uint8_t b = input.buttons;
    const float cy = std::cos(input.yaw);
    const float sy = std::sin(input.yaw);
    
    float dx = 0, dz = 0;
    if (b & static_cast<uint8_t>(InputButton::FORWARD))  { dx += -sy; dz += -cy; }
    if (b & static_cast<uint8_t>(InputButton::BACKWARD)) { dx -= -sy; dz -= -cy; }
    if (b & static_cast<uint8_t>(InputButton::LEFT))     { dx -= cy;  dz -= -sy; }
    if (b & static_cast<uint8_t>(InputButton::RIGHT))    { dx += cy;  dz += -sy; }
    
    // Use provided max speed if given, otherwise use effective speed with multipliers
    const int32_t speedLimit = (maxSpeed > 0) ? maxSpeed : getEffectiveSpeed();
    
    const float hlen = std::sqrt(dx*dx + dz*dz);
    const float scale = (hlen > 0.001f) ? (static_cast<float>(speedLimit) / hlen) : 0.0f;
    
    outVx = static_cast<int32_t>(dx * scale);
    outVz = static_cast<int32_t>(dz * scale);
}

bool WalkComponent::isWalking(const InputComponent& input) {
    const uint8_t b = input.buttons;
    return (b & static_cast<uint8_t>(InputButton::FORWARD))  != 0 ||
           (b & static_cast<uint8_t>(InputButton::BACKWARD)) != 0 ||
           (b & static_cast<uint8_t>(InputButton::LEFT))     != 0 ||
           (b & static_cast<uint8_t>(InputButton::RIGHT))    != 0;
}

} // namespace voxelmmo
