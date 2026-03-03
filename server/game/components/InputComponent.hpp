#pragma once
#include <cstdint>

namespace voxelmmo {

struct InputComponent {
    uint8_t buttons{0};   ///< InputButton bitmask
    float   yaw{0.0f};    ///< Camera horizontal angle (radians)
    float   pitch{0.0f};  ///< Camera vertical angle (radians) — ghost only
};

} // namespace voxelmmo
