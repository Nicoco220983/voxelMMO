#pragma once
#include "common/Types.hpp"

namespace voxelmmo {

/**
 * @brief Tracks which chunk an entity currently occupies.
 *
 * Chunk assignment is mandatory at entity creation. The component cannot be
 * default-constructed; a valid ChunkId must always be provided.
 * Managed exclusively by GameEngine::checkEntitiesChunks().
 */
struct ChunkMemberComponent {
    ChunkId currentChunkId;

    explicit ChunkMemberComponent(ChunkId chunkId) : currentChunkId(chunkId) {}
};

} // namespace voxelmmo
