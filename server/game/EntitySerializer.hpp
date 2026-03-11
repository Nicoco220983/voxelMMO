#pragma once
#include "common/Types.hpp"
#include "common/SafeBufWriter.hpp"
#include <entt/entt.hpp>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Entity serialization utilities for chunk state messages.
 *
 * Provides two serialization modes:
 * - serializeFull: Serializes all serializable components (snapshot). Ignores dirty flags
 *   but forces the CREATED_BIT in the output flags byte for consistency.
 * - serializeDelta: Serializes only components marked dirty in the specified flags field.
 *
 * Both methods write dirty flags in the serialization for consistency.
 */
class EntitySerializer {
public:
    /**
     * @brief Serialize all components of an entity (full snapshot mode).
     *
     * Ignores dirty component state and serializes all serializable components.
     * Forces the CREATED_BIT in the output flags byte for consistency.
     *
     * Output format:
     *   [GlobalEntityId(4)][EntityType(1)][flags(1)][component data...]
     *
     * @param reg Entity registry
     * @param ent Entity handle
     * @param w SafeBufWriter to write to
     * @return Number of bytes written
     */
    static size_t serializeFull(
        entt::registry& reg,
        entt::entity ent,
        SafeBufWriter& w);

    /**
     * @brief Serialize only dirty components of an entity (delta mode).
     *
     * Considers the dirty component and only serializes components whose bits
     * are set in the specified flags field. The dirty flags are included in
     * the output for consistency.
     *
     * Output format for CREATE/UPDATE:
     *   [deltaType(1)][GlobalEntityId(4)][EntityType(1)][flags(1)][component data...]
     *
     * Output format for DELETE:
     *   [deltaType(1)][GlobalEntityId(4)]
     *
     * Output format for CHUNK_CHANGE:
     *   [deltaType(1)][GlobalEntityId(4)][newChunkId(8)]
     *
     * @param reg Entity registry
     * @param ent Entity handle
     * @param dirtyFlags The dirty flags mask to use (from DirtyComponent)
     * @param isLeavingChunk Whether entity is leaving this chunk (for CHUNK_CHANGE)
     * @param isDeleted Whether entity is pending deletion (for DELETE)
     * @param w SafeBufWriter to write to
     * @return Number of bytes written, or 0 if nothing to serialize
     */
    static size_t serializeDelta(
        entt::registry& reg,
        entt::entity ent,
        uint8_t dirtyFlags,
        bool isLeavingChunk,
        bool isDeleted,
        SafeBufWriter& w);

private:
    /**
     * @brief Serialize component data based on component mask.
     *
     * @param reg Entity registry
     * @param ent Entity handle
     * @param componentMask Component mask indicating which components to serialize
     * @param w SafeBufWriter to write component data to
     */
    static void serializeComponents(
        entt::registry& reg,
        entt::entity ent,
        uint8_t componentMask,
        SafeBufWriter& w);
};

} // namespace voxelmmo
