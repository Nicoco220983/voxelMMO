#pragma once
#include "BaseEntity.hpp"
#include "common/Types.hpp"

namespace voxelmmo {

/**
 * @brief Player entity – extends BaseEntity with player-specific metadata.
 *
 * Tracks which chunk the player currently occupies so GameEngine::checkPlayersChunks()
 * can efficiently update the present/watching sets.
 */
class PlayerEntity : public BaseEntity {
public:
    PlayerId playerId;      ///< Owning player (maps to a gateway connection).
    ChunkId  currentChunk;  ///< Chunk the player's position falls into (updated every tick).

    PlayerEntity(entt::registry& reg, entt::entity ent,
                 EntityId eid, PlayerId pid)
        : BaseEntity(reg, ent, eid, EntityType::PLAYER)
        , playerId(pid)
        , currentChunk(ChunkId::make(0, 0, 0))
    {}
};

} // namespace voxelmmo
