#pragma once
#include "WorldChunk.hpp"
#include "common/Types.hpp"
#include "common/MessageTypes.hpp"
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
 * Serialization is driven by the three build*() methods which write into
 * external buffers provided by the caller (GameEngine's ChunkSerializer).
 * This allows GameEngine to concatenate all chunk messages into a single
 * buffer for efficient dispatch to gateways.
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

    explicit Chunk(ChunkId chunkId);

    /**
     * @brief Build a full snapshot and append it to outBuf.
     *
     * Voxels are LZ4-compressed directly from world.voxels.
     * Entities are serialized into the provided buffer.
     * Clears any existing deltas (the snapshot supersedes all previous deltas).
     *
     * @param reg The ECS registry.
     * @param tickCount Current server tick.
     * @param outBuf Output buffer to append the serialized message to.
     * @param scratch Reusable scratch buffer for LZ4 compression.
     * @return Number of bytes written to outBuf (0 on error).
     */
    size_t buildSnapshot(entt::registry& reg, uint32_t tickCount,
                         std::vector<uint8_t>& outBuf,
                         std::vector<uint8_t>& scratch);

    /**
     * @brief Build a snapshot delta and append it to outBuf.
     *
     * Payload is built into a local staging buffer, optionally LZ4-compressed
     * via scratch, then appended to outBuf.
     *
     * @param reg The ECS registry.
     * @param tickCount Current server tick.
     * @param outBuf Output buffer to append the serialized message to.
     * @param scratch Reusable scratch buffer for LZ4 compression.
     * @return Number of bytes written to outBuf (0 if nothing changed).
     */
    size_t buildSnapshotDelta(entt::registry& reg, uint32_t tickCount,
                              std::vector<uint8_t>& outBuf,
                              std::vector<uint8_t>& scratch);

    /**
     * @brief Build a tick delta and append it to outBuf.
     *
     * Same strategy as buildSnapshotDelta.
     *
     * @param reg The ECS registry.
     * @param tickCount Current server tick.
     * @param outBuf Output buffer to append the serialized message to.
     * @param scratch Reusable scratch buffer for LZ4 compression.
     * @return Number of bytes written to outBuf (0 if nothing changed).
     */
    size_t buildTickDelta(entt::registry& reg, uint32_t tickCount,
                          std::vector<uint8_t>& outBuf,
                          std::vector<uint8_t>& scratch);

    /**
     * @brief Clear dirty flags for entities in this chunk based on what was serialized.
     *
     * After a tick delta: clears tickDirtyFlags and tickDeltaType
     * After a snapshot delta: clears both tick AND snapshot flags (full reset)
     *
     * @param reg The ECS registry.
     * @param clearSnapshotFlags If true, clear both tick and snapshot flags (snapshot delta case).
     *                           If false, clear only tick flags (tick delta case).
     */
    void clearEntityDirtyFlags(entt::registry& reg, bool clearSnapshotFlags) const;

    /** @brief Byte length of the chunk message header (including entity_type + size prefix). */
    static constexpr size_t HEADER_SIZE = CHUNK_MESSAGE_HEADER_SIZE; // entity_type(1) + size(2) + ChunkId(8) + tick(4)

private:
    /**
     * @brief Write chunk message header into buf; return bytes written (= HEADER_SIZE).
     * Format: [type(1)][size(2)][chunk_id(8)][tick(4)]
     * The size field is written as 0 and must be filled in later when the final size is known.
     */
    size_t writeHeader(uint8_t* buf, ServerMessageType msgType, uint32_t tickCount) const;

    /**
     * @brief If buf's payload exceeds LZ4_COMPRESSION_THRESHOLD, copy the
     *        payload to scratch, LZ4-compress it back into buf
     *        (replacing the payload), prepend the uncompressed-size int32,
     *        and update the type byte to the *_COMPRESSED variant.
     */
    void maybeCompressDelta(std::vector<uint8_t>& buf, ServerMessageType compressedType,
                            std::vector<uint8_t>& scratch);

    /**
     * @brief Shared implementation for buildSnapshotDelta and buildTickDelta.
     *
     * @param voxelDeltas    The per-granularity changed-voxel list.
     * @param deltaTypeField Pointer-to-member selecting snapshotDeltaType or tickDeltaType.
     * @param flagsField     Pointer-to-member selecting snapshotDirtyFlags or tickDirtyFlags.
     * @param rawType        Message type written into the header (uncompressed variant).
     * @param compressedType Message type to use if LZ4 compression is applied.
     *
     * Note: Dirty flags are NOT cleared by this function. They are cleared centrally
     * by GameEngine after all serialization is complete.
     */
    size_t buildDeltaImpl(
        entt::registry& reg,
        uint32_t tickCount,
        const std::vector<std::pair<VoxelIndex, VoxelType>>& voxelDeltas,
        DeltaType DirtyComponent::* deltaTypeField,
        uint8_t DirtyComponent::* flagsField,
        ServerMessageType rawType,
        ServerMessageType compressedType,
        std::vector<uint8_t>& outBuf,
        std::vector<uint8_t>& scratch);
};

} // namespace voxelmmo
