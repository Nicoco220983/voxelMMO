#pragma once
#include "common/Types.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

/**
 * @brief Global entity identifier assigned at spawn.
 *
 * This component is emplaced on every entity when created. The ID is unique
 * across the entire server session and never changes, enabling stable entity
 * tracking across chunk boundaries and save/resume cycles.
 *
 * The ID is NOT serialized directly by this component; the serialization code
 * reads it and writes it as uint32 in the wire format.
 */
struct GlobalEntityIdComponent {
    GlobalEntityId id;
};

} // namespace voxelmmo
