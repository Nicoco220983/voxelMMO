#include "game/EntitySerializer.hpp"
#include "common/EntityCatalog.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "common/EntityType.hpp"
#include "common/NetworkProtocol.hpp"

namespace voxelmmo {

size_t EntitySerializer::serializeCreate(
    entt::registry& reg,
    entt::entity ent,
    SafeBufWriter& w,
    bool forDelta)
{
    const size_t startOffset = w.offset();
    
    // Get entity type and look up the appropriate serializer
    const auto etype = reg.get<EntityTypeComponent>(ent).type;
    const auto* info = EntityCatalog::instance().findById(static_cast<uint8_t>(etype));
    if (!info || !info->serializeCreate) {
        return 0;  // Unknown type or no serializer
    }
    
    // Write delta type prefix for delta contexts (CHUNK_TICK_DELTA CREATE_ENTITY)
    if (forDelta) {
        w.write(static_cast<uint8_t>(DeltaType::CREATE_ENTITY));
    }
    
    // Serialize entity state (no delta type prefix - that's handled above)
    info->serializeCreate(reg, ent, w);
    
    return w.offset() - startOffset;
}

size_t EntitySerializer::serializeDelta(
    entt::registry& reg,
    entt::entity ent,
    bool isLeavingChunk,
    SafeBufWriter& w)
{
    const size_t startOffset = w.offset();
    
    const auto& dirty = reg.get<DirtyComponent>(ent);
    
    // Handle special delta types that don't need entity-type-specific logic
    const bool isDeleted = (dirty.deltaType == DeltaType::DELETE_ENTITY);
    if (isDeleted) {
        const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
        w.write(static_cast<uint8_t>(DeltaType::DELETE_ENTITY));
        w.write(gid.id);
        return w.offset() - startOffset;
    }
    
    if (isLeavingChunk) {
        const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
        const auto& dyn = reg.get<DynamicPositionComponent>(ent);
        const ChunkId newChunkId = ChunkId::make(
            dyn.y >> CHUNK_SHIFT_Y,
            dyn.x >> CHUNK_SHIFT_X,
            dyn.z >> CHUNK_SHIFT_Z
        );
        w.write(static_cast<uint8_t>(DeltaType::CHUNK_CHANGE_ENTITY));
        w.write(gid.id);
        w.write(newChunkId.packed);
        return w.offset() - startOffset;
    }
    
    // For CREATE_ENTITY and UPDATE_ENTITY, use entity-type-specific serializer
    const auto etype = reg.get<EntityTypeComponent>(ent).type;
    const auto* info = EntityCatalog::instance().findById(static_cast<uint8_t>(etype));
    if (!info) {
        return 0;  // Unknown type
    }
    
    if (dirty.deltaType == DeltaType::CREATE_ENTITY) {
        // Newly created entity: write delta type + full state
        w.write(static_cast<uint8_t>(DeltaType::CREATE_ENTITY));
        if (info->serializeCreate) {
            info->serializeCreate(reg, ent, w);
        }
    } else if (dirty.dirtyFlags != 0 && info->serializeUpdate) {
        // Updated entity: write delta type + delta state
        w.write(static_cast<uint8_t>(DeltaType::UPDATE_ENTITY));
        info->serializeUpdate(reg, ent, dirty, w);
    }
    
    return w.offset() - startOffset;
}

} // namespace voxelmmo
