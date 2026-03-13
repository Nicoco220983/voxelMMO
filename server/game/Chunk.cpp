#include "game/Chunk.hpp"
#include "game/EntitySerializer.hpp"
#include "game/components/DirtyComponent.hpp"
#include "common/SafeBufWriter.hpp"
#include <lz4.h>
#include <algorithm>
#include <cstring>

namespace voxelmmo {

Chunk::Chunk(ChunkId chunkId) : id(chunkId) {
}

size_t Chunk::writeHeader(uint8_t* buf, ServerMessageType msgType, uint32_t tickCount) const {
    // Header format: [type(1)][size(2)][chunk_id(8)][tick(4)] = 15 bytes
    buf[0] = static_cast<uint8_t>(msgType);
    // size (bytes 1-2) is filled in later when final size is known
    buf[1] = 0;
    buf[2] = 0;
    std::memcpy(buf + 3, &id.packed, sizeof(int64_t));
    std::memcpy(buf + 11, &tickCount, sizeof(uint32_t));
    return HEADER_SIZE;
}

// ── Delta compression helper ──────────────────────────────────────────────

void Chunk::maybeCompressDelta(std::vector<uint8_t>& buf, ServerMessageType compressedType,
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

// ── Snapshot ──────────────────────────────────────────────────────────────

size_t Chunk::buildSnapshot(entt::registry& reg, uint32_t tickCount,
                            std::vector<uint8_t>& outBuf,
                            std::vector<uint8_t>& scratch)
{
    // Remember where this message starts in the output buffer
    const size_t msgStartOffset = outBuf.size();
    
    // ── Header (15 bytes) + flags byte ───────────────────────────────────
    const size_t headerOffset = outBuf.size();
    outBuf.resize(headerOffset + HEADER_SIZE + 1);  // [0..14] header, [15] flags
    writeHeader(outBuf.data() + headerOffset, ServerMessageType::CHUNK_SNAPSHOT_COMPRESSED, tickCount);
    // flags written at the end

    // ── Voxels: LZ4 directly from world.voxels ────────────────────────────
    const int voxelBound = LZ4_compressBound(static_cast<int>(CHUNK_VOXEL_COUNT));
    const size_t cvsSizeOff = outBuf.size();  // offset of int32 compressed_voxel_size
    outBuf.resize(outBuf.size() + sizeof(int32_t) + static_cast<size_t>(voxelBound));

    const int cvs = LZ4_compress_default(
        reinterpret_cast<const char*>(world.voxels.data()),
        reinterpret_cast<char*>(outBuf.data() + cvsSizeOff + sizeof(int32_t)),
        static_cast<int>(CHUNK_VOXEL_COUNT),
        voxelBound);

    const int32_t cvs32 = cvs;
    std::memcpy(outBuf.data() + cvsSizeOff, &cvs32, sizeof(int32_t));
    outBuf.resize(cvsSizeOff + sizeof(int32_t) + static_cast<size_t>(cvs));

    // ── Entities: serialize raw into scratch ──────────────────────────────
    scratch.clear();
    
    // Create writer with space reserved for count
    SafeBufWriter w{scratch, sizeof(int32_t)};
    
    int32_t entityCount = 0;
    for (auto ent : entities) {
        EntitySerializer::serializeFull(reg, ent, w);
        ++entityCount;
    }
    
    // Finalize buffer and write count at the beginning
    w.finalize();
    std::memcpy(scratch.data(), &entityCount, sizeof(int32_t));

    // ── Entity section: copy raw or LZ4-compress into outBuf ───────────────
    uint8_t flags = 0;
    const size_t entitySizeOff = outBuf.size();  // int32 entity_section_stored_size
    outBuf.resize(outBuf.size() + sizeof(int32_t));

    if (scratch.size() >= LZ4_COMPRESSION_THRESHOLD) {
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

        // stored_size covers the int32 uncompressed_size field + compressed bytes
        const int32_t storedSize = static_cast<int32_t>(sizeof(int32_t)) + ces;
        std::memcpy(outBuf.data() + entitySizeOff, &storedSize, sizeof(int32_t));
        outBuf.resize(uncompSizeOff + sizeof(int32_t) + static_cast<size_t>(ces));
        flags = 0x01;
    } else {
        const int32_t storedSize = static_cast<int32_t>(scratch.size());
        std::memcpy(outBuf.data() + entitySizeOff, &storedSize, sizeof(int32_t));
        outBuf.insert(outBuf.end(), scratch.begin(), scratch.end());
    }

    // Write flags byte
    outBuf[headerOffset + HEADER_SIZE] = flags;

    // Fill in the size field (bytes 1-2) now that we know the final size
    const uint16_t msgSize = static_cast<uint16_t>(outBuf.size() - headerOffset);
    outBuf[headerOffset + 1] = static_cast<uint8_t>(msgSize & 0xFF);
    outBuf[headerOffset + 2] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);

    return outBuf.size() - msgStartOffset;
}

// ── Delta (shared implementation) ─────────────────────────────────────────

size_t Chunk::buildDeltaImpl(
    entt::registry& reg,
    uint32_t tickCount,
    const std::vector<std::pair<VoxelIndex, VoxelType>>& voxelDeltas,
    DeltaType DirtyComponent::* deltaTypeField,
    uint8_t DirtyComponent::* flagsField,
    ServerMessageType rawType,
    ServerMessageType compressedType,
    std::vector<uint8_t>& outBuf,
    std::vector<uint8_t>& scratch)
{
    // Early exit: nothing to send
    if (voxelDeltas.empty()) {
        const bool anyDirty = std::any_of(entities.begin(), entities.end(),
            [&](entt::entity ent) {
                auto& dirty = reg.get<DirtyComponent>(ent);
                return dirty.*flagsField != 0 || dirty.*deltaTypeField != DeltaType::UPDATE_ENTITY;
            });
        // Also check for entities that left (CHUNK_CHANGE) or have pending operations
        const bool anyLeaving = !leftEntities.empty();
        if (!anyDirty && !anyLeaving) return 0;
    }

    const size_t msgStartOffset = outBuf.size();

    // Build into a local staging buffer first
    std::vector<uint8_t> staging;
    const size_t maxSize = HEADER_SIZE
        + sizeof(int32_t) + voxelDeltas.size() * (sizeof(uint16_t) + sizeof(uint8_t))
        + sizeof(int32_t) + entities.size() * 64;
    staging.resize(maxSize);

    size_t off = writeHeader(staging.data(), rawType, tickCount);

    // Voxel delta section
    const int32_t voxelCount = static_cast<int32_t>(voxelDeltas.size());
    std::memcpy(staging.data() + off, &voxelCount, sizeof(int32_t));
    off += sizeof(int32_t);
    for (const auto& [vidx, vtype] : voxelDeltas) {
        std::memcpy(staging.data() + off, &vidx, sizeof(VoxelIndex));
        off += sizeof(VoxelIndex);
        staging[off++] = vtype;
    }

    // Entity delta section
    const size_t entityCountOff = off;
    off += sizeof(int32_t);
    
    SafeBufWriter w{staging, off};
    int32_t entityCount = 0;
    
    // Helper lambda to process an entity
    auto processEntity = [&](entt::entity ent) {
        auto& dirty = reg.get<DirtyComponent>(ent);
        const uint8_t mask = dirty.*flagsField;
        const DeltaType deltaType = dirty.*deltaTypeField;
        const bool isDeleted = (deltaType == DeltaType::DELETE_ENTITY);
        const bool isLeaving = leftEntities.count(ent) > 0;
        const bool isNewlyCreated = (deltaType == DeltaType::CREATE_ENTITY);
        
        // Entities that entered this chunk (CREATE_ENTITY delta type) need full serialization
        // (not delta) because receiving clients may not have prior state for them.
        size_t bytesWritten = 0;
        if (isNewlyCreated) {
            bytesWritten = EntitySerializer::serializeFull(reg, ent, w, /*forDelta=*/true);
        } else if (mask || isDeleted || isLeaving || deltaType != DeltaType::UPDATE_ENTITY) {
            bytesWritten = EntitySerializer::serializeDelta(
                reg, ent, dirty, isLeaving, isDeleted, w);
        }
        
        if (bytesWritten > 0) {
            ++entityCount;
        }
    };
    
    // Process current entities
    for (auto ent : entities) {
        processEntity(ent);
    }
    
    // Process entities that left this chunk (for CHUNK_CHANGE_ENTITY messages)
    for (auto ent : leftEntities) {
        processEntity(ent);
    }
    
    off = w.offset();
    std::memcpy(staging.data() + entityCountOff, &entityCount, sizeof(int32_t));
    staging.resize(off);

    maybeCompressDelta(staging, compressedType, scratch);

    // Fill in the size field (bytes 1-2) now that staging is complete
    const uint16_t msgSize = static_cast<uint16_t>(staging.size());
    staging[1] = static_cast<uint8_t>(msgSize & 0xFF);
    staging[2] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);

    // Append staging to output buffer
    outBuf.insert(outBuf.end(), staging.begin(), staging.end());

    return outBuf.size() - msgStartOffset;
}

// ── Snapshot delta ────────────────────────────────────────────────────────

size_t Chunk::buildSnapshotDelta(entt::registry& reg, uint32_t tickCount,
                                  std::vector<uint8_t>& outBuf,
                                  std::vector<uint8_t>& scratch)
{
    // Note: GameEngine handles clearing previous deltas by tracking state externally.
    // This method just builds and appends the snapshot delta message.
    return buildDeltaImpl(reg, tickCount,
        world.voxelsSnapshotDeltas,
        &DirtyComponent::snapshotDeltaType,
        &DirtyComponent::snapshotDirtyFlags,
        ServerMessageType::CHUNK_SNAPSHOT_DELTA,
        ServerMessageType::CHUNK_SNAPSHOT_DELTA_COMPRESSED,
        outBuf, scratch);
}

// ── Tick delta ────────────────────────────────────────────────────────────

size_t Chunk::buildTickDelta(entt::registry& reg, uint32_t tickCount,
                              std::vector<uint8_t>& outBuf,
                              std::vector<uint8_t>& scratch)
{
    return buildDeltaImpl(reg, tickCount,
        world.voxelsTickDeltas,
        &DirtyComponent::tickDeltaType,
        &DirtyComponent::tickDirtyFlags,
        ServerMessageType::CHUNK_TICK_DELTA,
        ServerMessageType::CHUNK_TICK_DELTA_COMPRESSED,
        outBuf, scratch);
}

// ── Entity dirty flag clearing ─────────────────────────────────────────────

void Chunk::clearEntityDirtyFlags(entt::registry& reg, bool clearSnapshotFlags) const
{
    // Clear flags for all entities currently in this chunk
    for (auto ent : entities) {
        if (auto* dirty = reg.try_get<DirtyComponent>(ent)) {
            dirty->clearTick();
            if (clearSnapshotFlags) {
                // Don't clear snapshotDeltaType if it's CREATE_ENTITY - 
                // sendSelfEntityMessages() needs this to detect new players
                // and will clear it after sending SELF_ENTITY
                if (dirty->snapshotDeltaType != DeltaType::CREATE_ENTITY) {
                    dirty->snapshotDeltaType = DeltaType::UPDATE_ENTITY;
                }
                dirty->snapshotDirtyFlags = 0;
            }
        }
    }
    
    // Also clear flags for entities that left this chunk (they may still have pending deltas)
    for (auto ent : leftEntities) {
        if (auto* dirty = reg.try_get<DirtyComponent>(ent)) {
            dirty->clearTick();
            if (clearSnapshotFlags) {
                // Don't clear snapshotDeltaType if it's CREATE_ENTITY
                if (dirty->snapshotDeltaType != DeltaType::CREATE_ENTITY) {
                    dirty->snapshotDeltaType = DeltaType::UPDATE_ENTITY;
                }
                dirty->snapshotDirtyFlags = 0;
            }
        }
    }
}

} // namespace voxelmmo
