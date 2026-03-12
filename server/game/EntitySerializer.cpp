#include "game/EntitySerializer.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/SheepBehaviorComponent.hpp"
#include "game/components/PendingDeleteComponent.hpp"
#include "common/EntityType.hpp"
#include "common/MessageTypes.hpp"

namespace voxelmmo {

size_t EntitySerializer::serializeFull(
    entt::registry& reg,
    entt::entity ent,
    SafeBufWriter& w,
    bool forDelta)
{
    const size_t startOffset = w.offset();
    
    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    const auto& etypeComp = reg.get<EntityTypeComponent>(ent);
    const auto etype = etypeComp.type;
    
    // For delta contexts, write CREATE_ENTITY delta type first
    if (forDelta) {
        w.write(static_cast<uint8_t>(DeltaType::CREATE_ENTITY));
    }
    
    // Write GlobalEntityId and EntityType
    w.write(gid.id);
    w.write(static_cast<uint8_t>(etype));
    
    // Build flags: all applicable component bits (no CREATED_BIT needed, delta type indicates creation)
    uint8_t flags = POSITION_BIT;
    if (etype == EntityType::SHEEP) {
        flags |= SHEEP_BEHAVIOR_BIT;
    }
    
    w.write(flags);
    
    // Serialize components
    serializeComponents(reg, ent, flags, w);
    
    return w.offset() - startOffset;
}

size_t EntitySerializer::serializeDelta(
    entt::registry& reg,
    entt::entity ent,
    const DirtyComponent& dirty,
    bool isLeavingChunk,
    bool isDeleted,
    SafeBufWriter& w)
{
    const uint8_t dirtyFlags = dirty.tickDirtyFlags;
    
    // If no dirty flags and not deleted/leaving, nothing to serialize
    if (dirtyFlags == 0 && !isDeleted && !isLeavingChunk && dirty.tickDeltaType == DeltaType::UPDATE_ENTITY) {
        return 0;
    }
    
    const size_t startOffset = w.offset();
    
    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    const auto etype = reg.get<EntityTypeComponent>(ent).type;
    
    // Determine delta type (single value, not a bitmask).
    // Priority: DELETE > CHUNK_CHANGE > CREATE > UPDATE
    DeltaType deltaType;
    if (isDeleted) {
        deltaType = DeltaType::DELETE_ENTITY;
    } else if (isLeavingChunk) {
        deltaType = DeltaType::CHUNK_CHANGE_ENTITY;
    } else if (dirty.tickDeltaType == DeltaType::CREATE_ENTITY) {
        deltaType = DeltaType::CREATE_ENTITY;
    } else {
        deltaType = DeltaType::UPDATE_ENTITY;
    }
    
    w.write(static_cast<uint8_t>(deltaType));
    w.write(gid.id);
    
    // DELETE: just GlobalEntityId, no additional data
    if (deltaType == DeltaType::DELETE_ENTITY) {
        return w.offset() - startOffset;
    }
    
    // CHUNK_CHANGE: include new chunk ID computed from position, then done
    if (deltaType == DeltaType::CHUNK_CHANGE_ENTITY) {
        const auto& dyn = reg.get<DynamicPositionComponent>(ent);
        const ChunkId newChunkId = ChunkId::make(
            dyn.y >> CHUNK_SHIFT_Y,
            dyn.x >> CHUNK_SHIFT_X,
            dyn.z >> CHUNK_SHIFT_Z
        );
        w.write(newChunkId.packed);
        return w.offset() - startOffset;
    }
    
    // CREATE and UPDATE: include EntityType and component data
    w.write(static_cast<uint8_t>(etype));
    
    // Component mask: keep component bits 0-5
    uint8_t componentMask = dirtyFlags & 0x3F;
    w.write(componentMask);
    
    // Serialize components based on mask
    serializeComponents(reg, ent, componentMask, w);
    
    return w.offset() - startOffset;
}

void EntitySerializer::serializeComponents(
    entt::registry& reg,
    entt::entity ent,
    uint8_t componentMask,
    SafeBufWriter& w)
{
    if (componentMask & POSITION_BIT) {
        const auto& dyn = reg.get<DynamicPositionComponent>(ent);
        dyn.serializeFields(w);
    }
    
    if (componentMask & SHEEP_BEHAVIOR_BIT) {
        const auto& behavior = reg.get<SheepBehaviorComponent>(ent);
        behavior.serializeFields(w);
    }
}

} // namespace voxelmmo
