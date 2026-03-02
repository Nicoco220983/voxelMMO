#pragma once
#include "common/Types.hpp"

namespace voxelmmo {

/** @brief Emplaced only on player entities; replaces the old PlayerEntity::playerId field. */
struct PlayerComponent { PlayerId playerId; };

} // namespace voxelmmo
