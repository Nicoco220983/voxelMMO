#pragma once
#include "common/MessageTypes.hpp"

namespace voxelmmo {

/** @brief Stores the wire entity-type byte; emplaced on every entity at creation. */
struct EntityTypeComponent { EntityType type; };

} // namespace voxelmmo
