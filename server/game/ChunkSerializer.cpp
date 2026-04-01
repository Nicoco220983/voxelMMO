#include "game/ChunkSerializer.hpp"
#include "game/Chunk.hpp"
#include "game/ChunkRegistry.hpp"
#include "game/EntitySerializer.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "common/SafeBufWriter.hpp"
#include <entt/entt.hpp>
#include <lz4.h>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace voxelmmo {

// Byte length of the chunk message header (including entity_type + size prefix)
static constexpr size_t HEADER_SIZE = CHUNK_MESSAGE_HEADER_SIZE; // entity_type(1) + size(2) + ChunkId(8) + tick(4)

ChunkSerializer::ChunkSerializer() {
    // Pre-allocate buffers to avoid repeated allocations
    scratch.reserve(64 * 1024);
    staging.reserve(64 * 1024);
}

void ChunkSerializer::clear() {
    chunkBuf.clear();
    // scratch and staging kept for reuse (capacity preserved)
}

bool ChunkSerializer::hasChunkData() const {
    return !chunkBuf.empty();
}

bool ChunkSerializer::hasSelfEntityData() const {
    return !selfEntityBuf.empty();
}

const uint8_t* ChunkSerializer::getChunkData() const {
    return chunkBuf.data();
}

size_t ChunkSerializer::getChunkDataSize() const {
    return chunkBuf.size();
}

void ChunkSerializer::ensureScratch(size_t minSize) {
    if (scratch.capacity() < minSize) {
        scratch.reserve(minSize);
    }
}

void ChunkSerializer::ensureStaging(size_t minSize) {
    if (staging.capacity() < minSize) {
        staging.reserve(minSize);
    }
}

// ── Header helper ─────────────────────────────────────────────────────────

static size_t writeHeader(uint8_t* buf, ServerMessageType msgType, ChunkId chunkId, uint32_t tickCount) {
    // Header format: [type(1)][size(2)][chunk_id(8)][tick(4)] = 15 bytes
    buf[0] = static_cast<uint8_t>(msgType);
    // size (bytes 1-2) is filled in later when final size is known
    buf[1] = 0;
    buf[2] = 0;
    std::memcpy(buf + 3, &chunkId.packed, sizeof(int64_t));
    std::memcpy(buf + 11, &tickCount, sizeof(uint32_t));
    return HEADER_SIZE;
}

// ── Delta compression helper ──────────────────────────────────────────────

static void maybeCompressDelta(std::vector<uint8_t>& buf, ServerMessageType compressedType,
                                std::vector<uint8_t>& scratch) {
    const size_t payloadSize = buf.size() - HEADER_SIZE;
    if (payloadSize < LZ4_COMPRESSION_THRESHOLD) return;

    // Copy the raw payload into scratch
    scratch.assign(buf.data() + HEADER_SIZE, buf.data() + buf.size());

    // Compress scratch → buf  (replace payload with int32 uncompressed_size + LZ4 data)
    const int bound = LZ4_compressBound(static_cast<int>(scratch.size()));
    buf.resize(HEADER_SIZE + sizeof(int32_t) + static_cast<size_t>(bound));

    const int32_t uncompSize = static_cast<int32_t>(scratch.size());
    std::memcpy(buf.data() + HEADER_SIZE, &uncompSize, sizeof(int32_t));

    const int compSize = LZ4_compress_default(
        reinterpret_cast<const char*>(scratch.data()),
        reinterpret_cast<char*>(buf.data() + HEADER_SIZE + sizeof(int32_t)),
        static_cast<int>(scratch.size()),
        bound);

    buf.resize(HEADER_SIZE + sizeof(int32_t) + static_cast<size_t>(compSize));
    buf[0] = static_cast<uint8_t>(compressedType);
}

// ═══════════════════════════════════════════════════════════════════════════
// ═══ VOXEL SERIALIZATION ═══════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════

// ── Voxel Snapshot ─────────────────────────────────────────────────────────

/**
 * @brief Serialize all voxels as LZ4-compressed data.
 * @param chunk The chunk containing the voxel data.
 * @param outBuf Output buffer to append compressed voxel data to.
 * @return Number of bytes written for the voxel section.
 */
static size_t serializeVoxelsSnapshot(const Chunk& chunk, std::vector<uint8_t>& outBuf) {
    const size_t startOffset = outBuf.size();
    
    const int voxelBound = LZ4_compressBound(static_cast<int>(CHUNK_VOXEL_COUNT));
    const size_t cvsSizeOff = outBuf.size();  // offset of int32 compressed_voxel_size
    outBuf.resize(outBuf.size() + sizeof(int32_t) + static_cast<size_t>(voxelBound));

    const int cvs = LZ4_compress_default(
        reinterpret_cast<const char*>(chunk.world.voxels.data()),
        reinterpret_cast<char*>(outBuf.data() + cvsSizeOff + sizeof(int32_t)),
        static_cast<int>(CHUNK_VOXEL_COUNT),
        voxelBound);

    const int32_t cvs32 = cvs;
    std::memcpy(outBuf.data() + cvsSizeOff, &cvs32, sizeof(int32_t));
    outBuf.resize(cvsSizeOff + sizeof(int32_t) + static_cast<size_t>(cvs));
    
    return outBuf.size() - startOffset;
}

// ── Voxel Delta ────────────────────────────────────────────────────────────

/**
 * @brief Serialize voxel deltas to a buffer.
 * @param voxelDeltas List of changed voxels (index, type pairs).
 * @param buf Buffer to write to.
 * @param offset Starting offset in buffer.
 * @return New offset after writing voxel deltas.
 */
static size_t serializeVoxelsDelta(const std::vector<std::pair<VoxelIndex, VoxelType>>& voxelDeltas,
                                    std::vector<uint8_t>& buf, size_t offset) {
    const int32_t voxelCount = static_cast<int32_t>(voxelDeltas.size());
    std::memcpy(buf.data() + offset, &voxelCount, sizeof(int32_t));
    offset += sizeof(int32_t);
    
    for (const auto& [vidx, vtype] : voxelDeltas) {
        std::memcpy(buf.data() + offset, &vidx, sizeof(VoxelIndex));
        offset += sizeof(VoxelIndex);
        buf[offset++] = vtype;
    }
    
    return offset;
}

// ═══════════════════════════════════════════════════════════════════════════
// ═══ ENTITY SERIALIZATION ══════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════

// ── Entity Snapshot ────────────────────────────────────────────────────────

/**
 * @brief Serialize all entities in full (for snapshots).
 * @param chunk The chunk containing the entities.
 * @param reg The ECS registry.
 * @param outBuf Output buffer to append entity data to.
 * @param scratch Reusable scratch buffer for intermediate serialization.
 * @return flags byte value indicating compression status.
 */
static uint8_t serializeEntitiesSnapshot(const Chunk& chunk, entt::registry& reg,
                                          std::vector<uint8_t>& outBuf,
                                          std::vector<uint8_t>& scratch) {
    // Serialize entities into scratch buffer first
    scratch.clear();
    SafeBufWriter w{scratch, sizeof(int32_t)};
    
    int32_t entityCount = 0;
    for (auto ent : chunk.entities) {
        EntitySerializer::serializeFull(reg, ent, w);
        ++entityCount;
    }
    
    w.finalize();
    std::memcpy(scratch.data(), &entityCount, sizeof(int32_t));
    
    // Write entity section to output buffer
    const size_t entitySizeOff = outBuf.size();  // int32 entity_section_stored_size
    outBuf.resize(outBuf.size() + sizeof(int32_t));
    
    uint8_t flags = 0;
    if (scratch.size() >= LZ4_COMPRESSION_THRESHOLD) {
        // Compress entity data
        const int entBound = LZ4_compressBound(static_cast<int>(scratch.size()));
        const size_t uncompSizeOff = outBuf.size();  // int32 entity_uncompressed_size
        outBuf.resize(outBuf.size() + sizeof(int32_t) + static_cast<size_t>(entBound));
        
        const int32_t uncompSize = static_cast<int32_t>(scratch.size());
        std::memcpy(outBuf.data() + uncompSizeOff, &uncompSize, sizeof(int32_t));
        
        const int ces = LZ4_compress_default(
            reinterpret_cast<const char*>(scratch.data()),
            reinterpret_cast<char*>(outBuf.data() + uncompSizeOff + sizeof(int32_t)),
            static_cast<int>(scratch.size()),
            entBound);
        
        const int32_t storedSize = static_cast<int32_t>(sizeof(int32_t)) + ces;
        std::memcpy(outBuf.data() + entitySizeOff, &storedSize, sizeof(int32_t));
        outBuf.resize(uncompSizeOff + sizeof(int32_t) + static_cast<size_t>(ces));
        flags = 0x01;
    } else {
        // Store uncompressed
        const int32_t storedSize = static_cast<int32_t>(scratch.size());
        std::memcpy(outBuf.data() + entitySizeOff, &storedSize, sizeof(int32_t));
        outBuf.insert(outBuf.end(), scratch.begin(), scratch.end());
    }
    
    return flags;
}

// ── Entity Delta ───────────────────────────────────────────────────────────

/**
 * @brief Serialize entity deltas to a buffer (for tick deltas only).
 * Snapshot deltas now send full entities like snapshots.
 * @param chunk The chunk containing the entities.
 * @param reg The ECS registry.
 * @param buf Buffer to write to.
 * @param offset Starting offset in buffer.
 * @return New offset after writing entity deltas.
 */
static size_t serializeEntitiesDelta(const Chunk& chunk, entt::registry& reg,
                                      std::vector<uint8_t>& buf, size_t offset) {
    const size_t entityCountOff = offset;
    offset += sizeof(int32_t);
    
    SafeBufWriter w{buf, offset};
    int32_t entityCount = 0;
    
    auto processEntity = [&](entt::entity ent) {
        const bool isLeaving = chunk.leftEntities.count(ent) > 0;
        
        size_t bytesWritten = EntitySerializer::serializeDelta(
            reg, ent, isLeaving, w);
        
        if (bytesWritten > 0) {
            ++entityCount;
        }
    };
    
    // Process current entities
    for (auto ent : chunk.entities) {
        processEntity(ent);
    }
    
    // Process entities that left this chunk
    for (auto ent : chunk.leftEntities) {
        processEntity(ent);
    }
    
    offset = w.offset();
    std::memcpy(buf.data() + entityCountOff, &entityCount, sizeof(int32_t));
    
    return offset;
}

// ═══════════════════════════════════════════════════════════════════════════
// ═══ MESSAGE BUILDERS ══════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════

// ── Snapshot ──────────────────────────────────────────────────────────────

static size_t buildSnapshot(const Chunk& chunk, entt::registry& reg, uint32_t tickCount,
                            std::vector<uint8_t>& outBuf,
                            std::vector<uint8_t>& scratch) {
    const size_t msgStartOffset = outBuf.size();
    
    // Header (15 bytes) + flags byte
    const size_t headerOffset = outBuf.size();
    outBuf.resize(headerOffset + HEADER_SIZE + 1);
    writeHeader(outBuf.data() + headerOffset, ServerMessageType::CHUNK_SNAPSHOT_COMPRESSED, chunk.id, tickCount);
    
    // Serialize voxels
    serializeVoxelsSnapshot(chunk, outBuf);
    
    // Serialize entities (returns flags)
    uint8_t flags = serializeEntitiesSnapshot(chunk, reg, outBuf, scratch);
    
    // Write flags byte
    outBuf[headerOffset + HEADER_SIZE] = flags;
    
    // Fill in the size field
    const uint16_t msgSize = static_cast<uint16_t>(outBuf.size() - headerOffset);
    outBuf[headerOffset + 1] = static_cast<uint8_t>(msgSize & 0xFF);
    outBuf[headerOffset + 2] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    
    return outBuf.size() - msgStartOffset;
}

// ── Tick Delta (delta implementation) ─────────────────────────────────────

static size_t buildTickDelta(
    const Chunk& chunk,
    entt::registry& reg,
    uint32_t tickCount,
    std::vector<uint8_t>& outBuf,
    std::vector<uint8_t>& scratch,
    std::vector<uint8_t>& staging) {
    
    const auto& voxelDeltas = chunk.world.voxelsDeltas;
    
    // Early exit: nothing to send
    if (voxelDeltas.empty()) {
        const bool anyDirty = std::any_of(chunk.entities.begin(), chunk.entities.end(),
            [&](entt::entity ent) {
                auto& dirty = reg.get<DirtyComponent>(ent);
                return dirty.dirtyFlags != 0 || dirty.deltaType != DeltaType::UPDATE_ENTITY;
            });
        const bool anyLeaving = !chunk.leftEntities.empty();
        if (!anyDirty && !anyLeaving) return 0;
    }
    
    const size_t msgStartOffset = outBuf.size();
    
    // Resize staging buffer for this message
    const size_t maxSize = HEADER_SIZE
        + sizeof(int32_t) + voxelDeltas.size() * (sizeof(uint16_t) + sizeof(uint8_t))
        + sizeof(int32_t) + chunk.entities.size() * 64;
    staging.resize(maxSize);
    
    size_t off = writeHeader(staging.data(), ServerMessageType::CHUNK_TICK_DELTA, chunk.id, tickCount);
    
    // Serialize voxel deltas
    off = serializeVoxelsDelta(voxelDeltas, staging, off);
    
    // Serialize entity deltas
    off = serializeEntitiesDelta(chunk, reg, staging, off);
    staging.resize(off);
    
    // Compress if beneficial
    maybeCompressDelta(staging, ServerMessageType::CHUNK_TICK_DELTA_COMPRESSED, scratch);
    
    // Fill in the size field
    const uint16_t msgSize = static_cast<uint16_t>(staging.size());
    staging[1] = static_cast<uint8_t>(msgSize & 0xFF);
    staging[2] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    
    // Append to output buffer
    outBuf.insert(outBuf.end(), staging.begin(), staging.end());
    
    return outBuf.size() - msgStartOffset;
}

// ── Snapshot delta ────────────────────────────────────────────────────────

/**
 * @brief Build a snapshot delta message that sends full entities (like a snapshot).
 * This is used for periodic full-state syncs (every N ticks after initial snapshot).
 */
static size_t buildSnapshotDelta(const Chunk& chunk, entt::registry& reg, uint32_t tickCount,
                                  std::vector<uint8_t>& outBuf,
                                  std::vector<uint8_t>& scratch,
                                  std::vector<uint8_t>& staging) {
    const size_t msgStartOffset = outBuf.size();
    
    // Header (15 bytes) + flags byte
    const size_t headerOffset = outBuf.size();
    outBuf.resize(headerOffset + HEADER_SIZE + 1);
    writeHeader(outBuf.data() + headerOffset, ServerMessageType::CHUNK_SNAPSHOT_DELTA_COMPRESSED, chunk.id, tickCount);
    
    // Serialize voxels (full snapshot, not delta)
    serializeVoxelsSnapshot(chunk, outBuf);
    
    // Serialize entities (full entities, not deltas - returns flags)
    uint8_t flags = serializeEntitiesSnapshot(chunk, reg, outBuf, scratch);
    
    // Write flags byte
    outBuf[headerOffset + HEADER_SIZE] = flags;
    
    // Fill in the size field
    const uint16_t msgSize = static_cast<uint16_t>(outBuf.size() - headerOffset);
    outBuf[headerOffset + 1] = static_cast<uint8_t>(msgSize & 0xFF);
    outBuf[headerOffset + 2] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
    
    return outBuf.size() - msgStartOffset;
}

// ── Entity dirty flag clearing ─────────────────────────────────────────────

static void clearEntityDirtyFlags(const Chunk& chunk, entt::registry& reg, bool clearSnapshotFlags) {
    
    // Clear flags for all entities currently in this chunk
    for (auto ent : chunk.entities) {
        auto& dirty = reg.get<DirtyComponent>(ent);
        dirty.clear();
        if (clearSnapshotFlags) {
            // Don't clear snapshotDeltaType if it's CREATE_ENTITY - 
            // sendSelfEntityMessages() needs this to detect new players
            if (dirty.snapshotDeltaType != DeltaType::CREATE_ENTITY) {
                dirty.snapshotDeltaType = DeltaType::UPDATE_ENTITY;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ═══ PUBLIC ORCHESTRATION ══════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════

size_t ChunkSerializer::serializeAllChunks(entt::registry& registry, ChunkRegistry& chunkRegistry, uint32_t tick) {
    ensureScratch(64 * 1024);
    ensureStaging(64 * 1024);
    size_t chunksSerialized = 0;
    
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        auto& serState = chunkStates_[cid];
        
        size_t bytesWritten = 0;
        bool isSnapshot = false;
        bool isSnapshotDelta = false;
        
        if (!serState.hasBeenSerialized) {
            // First time serializing this chunk: build full snapshot
            bytesWritten = buildSnapshot(*chunkPtr, registry, tick, chunkBuf, scratch);
            serState.hasBeenSerialized = true;
            serState.lastSnapshotTick = tick;
            serState.deltaCount = 0;
            isSnapshot = true;
        } else {
            // Check if we should build snapshot delta (every 20 ticks or first delta after snapshot)
            if (serState.deltaCount == 0 || serState.deltaCount % 20 == 0) {
                bytesWritten = buildSnapshotDelta(*chunkPtr, registry, tick, chunkBuf, scratch, staging);
                if (bytesWritten > 0) {
                    serState.lastSnapshotTick = tick;
                    serState.deltaCount = 0;
                    isSnapshotDelta = true;
                }
            } else {
                // Build tick delta
                bytesWritten = buildTickDelta(*chunkPtr, registry, tick, chunkBuf, scratch, staging);
            }
        }
        
        if (bytesWritten > 0) {
            serState.deltaCount++;
            chunksSerialized++;
            
            // Clear dirty flags based on what was serialized
            clearEntityDirtyFlags(*chunkPtr, registry, isSnapshot || isSnapshotDelta);
        }
    }
    
    return chunksSerialized;
}

} // namespace voxelmmo
