#pragma once
#include "common/SafeBufWriter.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

// Forward declarations
struct DirtyComponent;

/**
 * @brief Entity serialization utilities for network transmission.
 *
 * All serialization functions write to a SafeBufWriter and return bytes written.
 * The caller is responsible for:
 *   - Writing message headers (type, size, chunk_id, tick)
 *   - Managing entity counts
 *   - Finalizing/compressing the buffer
 *
 * Serialization formats:
 *   serializeFull():     [delta_type if forDelta] [global_id(4)] [entity_type(1)] [component_mask(1)] [component_data...]
 *   serializeDelta():    [delta_type(1)] [global_id(4)] [entity_type(1) if CREATE/UPDATE] [component_mask(1) if CREATE/UPDATE] [component_data...]
 */
struct EntitySerializer {
    /**
     * @brief Serialize a full entity (all components).
     *
     * Used for snapshots and for newly created entities in deltas.
     * When forDelta=true, writes CREATE_ENTITY delta type as first byte.
     *
     * @param reg      Entity registry.
     * @param ent      Entity handle.
     * @param w        Buffer writer.
     * @param forDelta If true, write delta type prefix (default: false).
     * @return Bytes written.
     */
    static size_t serializeFull(
        entt::registry& reg,
        entt::entity ent,
        SafeBufWriter& w,
        bool forDelta = false);

    /**
     * @brief Serialize entity delta based on dirty flags.
     *
     * Determines delta type from dirty component state:
     *   - DELETE_ENTITY: isDeleted=true
     *   - CHUNK_CHANGE_ENTITY: isLeavingChunk=true
     *   - CREATE_ENTITY: dirty.tickDeltaType == CREATE_ENTITY
     *   - UPDATE_ENTITY: otherwise
     *
     * @param reg           Entity registry.
     * @param ent           Entity handle.
     * @param dirty         DirtyComponent (passed by const ref to access delta type).
     * @param isLeavingChunk Entity is leaving this chunk (CHUNK_CHANGE).
     * @param isDeleted     Entity is deleted (DELETE_ENTITY).
     * @param w             Buffer writer.
     * @return Bytes written (0 if nothing to serialize).
     */
    static size_t serializeDelta(
        entt::registry& reg,
        entt::entity ent,
        const DirtyComponent& dirty,
        bool isLeavingChunk,
        bool isDeleted,
        SafeBufWriter& w);

    /**
     * @brief Serialize component data based on component mask.
     *
     * Writes component fields in order:
     *   - POSITION_BIT: x,y,z,vx,vy,vz,grounded (DynamicPositionComponent)
     *   - SHEEP_BEHAVIOR_BIT: state,timer (SheepBehaviorComponent)
     *
     * @param reg           Entity registry.
     * @param ent           Entity handle.
     * @param componentMask Bitmask of components to serialize.
     * @param w             Buffer writer.
     */
    static void serializeComponents(
        entt::registry& reg,
        entt::entity ent,
        uint8_t componentMask,
        SafeBufWriter& w);
};

} // namespace voxelmmo
