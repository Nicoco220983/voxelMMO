#include "game/EntityTypeSerializers.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/SheepEntity.hpp"

namespace voxelmmo {

/**
 * @brief Serialization function table indexed by EntityType.
 *
 * This table maps each EntityType to its corresponding serializeFull
 * and serializeUpdate functions. The table is indexed by the uint8_t
 * value of the EntityType enum.
 */
const EntitySerializerTable ENTITY_SERIALIZER_TABLE[] = {
    // EntityType::PLAYER = 0
    {
        &PlayerEntity::serializeFull,
        &PlayerEntity::serializeUpdate
    },
    // EntityType::GHOST_PLAYER = 1
    {
        &GhostPlayerEntity::serializeFull,
        &GhostPlayerEntity::serializeUpdate
    },
    // EntityType::SHEEP = 2
    {
        &SheepEntity::serializeFull,
        &SheepEntity::serializeUpdate
    }
};

} // namespace voxelmmo
