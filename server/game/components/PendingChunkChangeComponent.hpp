#pragma once
#include "common/Types.hpp"

namespace voxelmmo {

/**
 * @brief Marks an entity that needs to move to a different chunk.
 *
 * Set by ChunkMembershipSystem when entity crosses chunk boundary.
 * Processed by EntityStateSystem to execute the actual move.
 *
 * The old chunk will send a CHUNK_CHANGE_ENTITY delta to its watchers,
 * the new chunk will send a CREATE_ENTITY delta.
 */
struct PendingChunkChangeComponent {
    ChunkId newChunkId;
    explicit PendingChunkChangeComponent(ChunkId chunkId) : newChunkId(chunkId) {}
};

} // namespace voxelmmo
