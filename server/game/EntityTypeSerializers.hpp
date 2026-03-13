#pragma once
#include "common/EntityType.hpp"
#include "common/NetworkProtocol.hpp"
#include "common/SafeBufWriter.hpp"
#include "common/Types.hpp"
#include <entt/entt.hpp>
#include <cstdint>

namespace voxelmmo {

// Forward declarations
struct DirtyComponent;

/**
 * @brief Function type for serializing full entity state (no delta type prefix).
 *
 * Writes entity record:
 *   [global_id(4)] [entity_type(1)] [component_mask(1)] [component_data...]
 *
 * @param reg Entity registry.
 * @param ent Entity handle.
 * @param w Buffer writer.
 * @return Bytes written.
 */
using SerializeFullFn = size_t(*)(entt::registry& reg, entt::entity ent, SafeBufWriter& w);

/**
 * @brief Function type for serializing entity update (delta state, no delta type prefix).
 *
 * Writes update record:
 *   [global_id(4)] [entity_type(1)] [component_mask(1)] [component_data...]
 *
 * The component_mask contains only dirty components.
 *
 * @param reg Entity registry.
 * @param ent Entity handle.
 * @param dirty DirtyComponent containing dirty flags.
 * @param w Buffer writer.
 * @return Bytes written (0 if nothing dirty).
 */
using SerializeUpdateFn = size_t(*)(entt::registry& reg, entt::entity ent, const DirtyComponent& dirty, SafeBufWriter& w);

/**
 * @brief Serialization function table entry per entity type.
 */
struct EntitySerializerTable {
    SerializeFullFn serializeFull;      // Full entity state (no delta type prefix)
    SerializeUpdateFn serializeUpdate;  // Delta state (no delta type prefix)
};

/**
 * @brief Serialization function table indexed by EntityType.
 *
 * Usage:
 *   auto& table = ENTITY_SERIALIZER_TABLE[static_cast<uint8_t>(entityType)];
 *   table.serializeFull(reg, ent, writer);   // For snapshots/CREATE_ENTITY
 *   table.serializeUpdate(reg, ent, dirty, writer);  // For UPDATE_ENTITY deltas
 */
extern const EntitySerializerTable ENTITY_SERIALIZER_TABLE[];

/**
 * @brief Helper to look up serializer table entry for an entity type.
 * @return Reference to the serializer table entry.
 */
inline const EntitySerializerTable& getEntitySerializer(EntityType type) {
    return ENTITY_SERIALIZER_TABLE[static_cast<uint8_t>(type)];
}

} // namespace voxelmmo
