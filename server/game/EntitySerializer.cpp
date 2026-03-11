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
    SafeBufWriter& w)
{
    const size_t startOffset = w.offset();
    
    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    const auto& etypeComp = reg.get<EntityTypeComponent>(ent);
    const auto etype = etypeComp.type;
    
    // Write GlobalEntityId and EntityType
    w.write(gid.id);
    w.write(static_cast<uint8_t>(etype));
    
    // Build flags: all applicable component bits + forced CREATED_BIT
    uint8_t flags = POSITION_BIT;
    if (etype == EntityType::SHEEP) {
        flags |= SHEEP_BEHAVIOR_BIT;
    }
    // Force CREATED_BIT for full serialization consistency
    flags |= DirtyComponent::CREATED_BIT;
    
    w.write(flags);
    
    // Serialize components
    serializeComponents(reg, ent, flags, w);
    
    return w.offset() - startOffset;
}

size_t EntitySerializer::serializeDelta(
    entt::registry& reg,
    entt::entity ent,
    uint8_t dirtyFlags,
    bool isLeavingChunk,
    bool isDeleted,
    SafeBufWriter& w)
{
    // If no dirty flags and not deleted/leaving, nothing to serialize
    if (dirtyFlags == 0 && !isDeleted && !isLeavingChunk) {
        return 0;
    }
    
    const size_t startOffset = w.offset();
    
    const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
    const auto etype = reg.get<EntityTypeComponent>(ent).type;
    
    // Build delta type mask
    uint8_t deltaType = 0;
    if (isDeleted) {
        deltaType |= static_cast<uint8_t>(DeltaType::DELETE_ENTITY);
    }
    if (isLeavingChunk) {
        deltaType |= static_cast<uint8_t>(DeltaType::CHUNK_CHANGE_ENTITY);
    }
    if (dirtyFlags & DirtyComponent::CREATED_BIT) {
        deltaType |= static_cast<uint8_t>(DeltaType::CREATE_ENTITY);
    }
    if (dirtyFlags & 0x3F) {  // Has component changes (bits 0-5)
        deltaType |= static_cast<uint8_t>(DeltaType::UPDATE_ENTITY);
    }
    
    w.write(deltaType);
    w.write(gid.id);
    
    // DELETE: just GlobalEntityId, no additional data
    if (isDeleted) {
        return w.offset() - startOffset;
    }
    
    // CHUNK_CHANGE: include new chunk ID computed from position
    if (isLeavingChunk) {
        const auto& dyn = reg.get<DynamicPositionComponent>(ent);
        const ChunkId newChunkId = ChunkId::make(
            dyn.y >> CHUNK_SHIFT_Y,
            dyn.x >> CHUNK_SHIFT_X,
            dyn.z >> CHUNK_SHIFT_Z
        );
        w.write(newChunkId.packed);
    }
    
    // CREATE and UPDATE: include EntityType and component data
    w.write(static_cast<uint8_t>(etype));
    
    // Component mask: keep component bits 0-5, preserve CREATED_BIT for new entities
    uint8_t componentMask = dirtyFlags & 0x3F;
    if (dirtyFlags & DirtyComponent::CREATED_BIT) {
        componentMask |= DirtyComponent::CREATED_BIT;
    }
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
