#pragma once
#include "common/Types.hpp"

namespace voxelmmo {

/**
 * @brief Tracks which chunk an entity currently occupies.
 *
 * Replaces the currentChunkId / chunkAssigned fields previously carried by BaseEntity.
 * Managed exclusively by GameEngine::checkEntitiesChunks().
 */
struct ChunkMemberComponent {
    ChunkId currentChunkId{};
    bool    chunkAssigned{false};
};

} // namespace voxelmmo
