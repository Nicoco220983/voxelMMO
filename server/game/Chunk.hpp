#pragma once
#include "WorldChunk.hpp"
#include "common/Types.hpp"
#include "common/NetworkProtocol.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include <entt/entt.hpp>
#include <set>
#include <memory>
#include <unordered_map>
#include <cstdint>

namespace voxelmmo {
    // Chunk header: [entity_type(1)][size(2)][chunk_id(8)][tick(4)] = 15 bytes
    static constexpr size_t CHUNK_MESSAGE_HEADER_SIZE = 1 + 2 + 8 + 4;
}

namespace voxelmmo {
    class WorldGenerator;
}

namespace voxelmmo {

/**
 * @brief Owns all simulation state for one chunk tile.
 *
 * This is a pure data structure. Serialization is handled by ChunkSerializer.
 */
class Chunk {
public:
    ChunkId id;

    /** @brief Players whose avatar is inside this chunk. */
    std::set<PlayerId> presentPlayers;

    /** @brief Players watching this chunk (present + nearby). */
    std::set<PlayerId> watchingPlayers;

    WorldChunk world;

    /**
     * @brief Set of entities currently resident in this chunk.
     *
     * Entities are tracked by their entt handle. The GlobalEntityId (stable across
     * chunk moves) is stored in GlobalEntityIdComponent and used on the wire.
     */
    std::set<entt::entity> entities;
    
    /**
     * @brief Set of entities that have left this chunk this tick.
     *
     * These entities have been removed from `entities` and are waiting
     * to be added to their new chunk. After processing, this set is cleared.
     */
    std::set<entt::entity> leftEntities;

    /** @brief True if the chunk has been activated (entities spawned). */
    bool activated = false;

    explicit Chunk(ChunkId chunkId) : id(chunkId) {}
};

} // namespace voxelmmo
