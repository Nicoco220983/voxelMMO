#pragma once
#include "common/Types.hpp"

namespace voxelmmo {

/**
 * @brief Marks an entity for creation in a specific chunk.
 *
 * Set during entity spawning (e.g., player join, mob generation).
 * Processed by ChunkMembershipSystem::updateEntitiesChunks() to add
 * the entity to the chunk's entity set.
 */
struct PendingCreateComponent {
    ChunkId targetChunkId;
    explicit PendingCreateComponent(ChunkId chunkId) : targetChunkId(chunkId) {}
};

} // namespace voxelmmo
