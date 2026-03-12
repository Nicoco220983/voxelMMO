#include "game/Chunk.hpp"
#include "game/EntitySerializer.hpp"
#include "game/components/DirtyComponent.hpp"
#include "common/SafeBufWriter.hpp"
#include <lz4.h>
#include <algorithm>
#include <cstring>

namespace voxelmmo {

Chunk::Chunk(ChunkId chunkId) : id(chunkId) {
    // Reserve scratch once; it grows on demand but rarely needs to.
    state.scratch.reserve(64 * 1024);
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

void Chunk::maybeCompressDelta(std::vector<uint8_t>& buf, ServerMessageType compressedType) {
    const size_t payloadSize = buf.size() - HEADER_SIZE;
    if (payloadSize < LZ4_COMPRESSION_THRESHOLD) return;

    // Copy the raw payload into scratch
    auto& scratch = state.scratch;
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

const std::vector<uint8_t>& Chunk::buildSnapshot(entt::registry& reg, uint32_t tickCount)
{
    // Clear any existing state - snapshot supersedes all deltas
    state.clear();
    
    auto& buf = state.buffer;

    // ── Header (15 bytes) + flags byte ───────────────────────────────────
    buf.resize(HEADER_SIZE + 1);  // [0..14] header, [15] flags
    writeHeader(buf.data(), ServerMessageType::CHUNK_SNAPSHOT_COMPRESSED, tickCount);
    // buf[HEADER_SIZE] = flags — written at the end

    // ── Voxels: LZ4 directly from world.voxels (zero intermediate copy) ──
    const int voxelBound = LZ4_compressBound(static_cast<int>(CHUNK_VOXEL_COUNT));
    const size_t cvsSizeOff = buf.size();  // offset of int32 compressed_voxel_size
    buf.resize(buf.size() + sizeof(int32_t) + static_cast<size_t>(voxelBound));

    const int cvs = LZ4_compress_default(
        reinterpret_cast<const char*>(world.voxels.data()),
        reinterpret_cast<char*>(buf.data() + cvsSizeOff + sizeof(int32_t)),
        static_cast<int>(CHUNK_VOXEL_COUNT),
        voxelBound);

    const int32_t cvs32 = cvs;
    std::memcpy(buf.data() + cvsSizeOff, &cvs32, sizeof(int32_t));
    buf.resize(cvsSizeOff + sizeof(int32_t) + static_cast<size_t>(cvs));

    // ── Entities: serialize raw into scratch ──────────────────────────────
    auto& scratch = state.scratch;
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

    // ── Entity section: copy raw or LZ4-compress into buf ─────────────────
    uint8_t flags = 0;
    const size_t entitySizeOff = buf.size();  // int32 entity_section_stored_size
    buf.resize(buf.size() + sizeof(int32_t));

    if (scratch.size() >= LZ4_COMPRESSION_THRESHOLD) {
        const int entBound = LZ4_compressBound(static_cast<int>(scratch.size()));
        const size_t uncompSizeOff = buf.size();  // int32 entity_uncompressed_size
        buf.resize(buf.size() + sizeof(int32_t) + static_cast<size_t>(entBound));

        const int32_t uncompSize = static_cast<int32_t>(scratch.size());
        std::memcpy(buf.data() + uncompSizeOff, &uncompSize, sizeof(int32_t));

        const int ces = LZ4_compress_default(
            reinterpret_cast<const char*>(scratch.data()),
            reinterpret_cast<char*>(buf.data() + uncompSizeOff + sizeof(int32_t)),
            static_cast<int>(scratch.size()),
            entBound);

        // stored_size covers the int32 uncompressed_size field + compressed bytes
        const int32_t storedSize = static_cast<int32_t>(sizeof(int32_t)) + ces;
        std::memcpy(buf.data() + entitySizeOff, &storedSize, sizeof(int32_t));
        buf.resize(uncompSizeOff + sizeof(int32_t) + static_cast<size_t>(ces));
        flags = 0x01;
    } else {
        // For uncompressed, also need to fill in size
        const uint16_t msgSize = static_cast<uint16_t>(buf.size());
        buf[1] = static_cast<uint8_t>(msgSize & 0xFF);
        buf[2] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);
        const int32_t storedSize = static_cast<int32_t>(scratch.size());
        std::memcpy(buf.data() + entitySizeOff, &storedSize, sizeof(int32_t));
        buf.insert(buf.end(), scratch.begin(), scratch.end());
    }

    buf[HEADER_SIZE] = flags;

    // Fill in the size field (bytes 1-2) now that we know the final size
    const uint16_t msgSize = static_cast<uint16_t>(buf.size());
    buf[1] = static_cast<uint8_t>(msgSize & 0xFF);
    buf[2] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);

    // Record this entry
    state.entries.push_back({tickCount, 0, buf.size()});
    state.hasNewData = true;

    return buf;
}

// ── Delta (shared implementation) ─────────────────────────────────────────

bool Chunk::buildDeltaImpl(
    entt::registry& reg,
    uint32_t tickCount,
    const std::vector<std::pair<VoxelIndex, VoxelType>>& voxelDeltas,
    DeltaType DirtyComponent::* deltaTypeField,
    uint8_t DirtyComponent::* flagsField,
    ServerMessageType rawType,
    ServerMessageType compressedType)
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
        if (!anyDirty && !anyLeaving) return false;
    }

    // Build into a local staging buffer
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
            // For entered/created entities, we still check if there's anything to send
            // (either they have dirty flags OR they're just entering/being created)
            bytesWritten = EntitySerializer::serializeFull(reg, ent, w, /*forDelta=*/true);
        } else if (mask || isDeleted || isLeaving || deltaType != DeltaType::UPDATE_ENTITY) {
            bytesWritten = EntitySerializer::serializeDelta(
                reg, ent, dirty, isLeaving, isDeleted, w);
        }
        
        if (bytesWritten > 0) {
            ++entityCount;
        }

        // Note: Dirty flags are cleared centrally in GameEngine::clearAllDirtyFlags()
        // after all serialization is complete for the tick.
    };
    
    // Process current entities
    for (auto ent : entities) {
        processEntity(ent);
    }
    
    // Process entities that left this chunk (for CHUNK_CHANGE_ENTITY messages)
    // These are not in 'entities' anymore but still need to be serialized
    for (auto ent : leftEntities) {
        processEntity(ent);
    }
    
    off = w.offset();
    std::memcpy(staging.data() + entityCountOff, &entityCount, sizeof(int32_t));
    staging.resize(off);

    maybeCompressDelta(staging, compressedType);

    // Fill in the size field (bytes 1-2) now that staging is complete
    const uint16_t msgSize = static_cast<uint16_t>(staging.size());
    staging[1] = static_cast<uint8_t>(msgSize & 0xFF);
    staging[2] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);

    // Append to the unified buffer
    size_t offset = state.buffer.size();
    state.entries.push_back({tickCount, offset, staging.size()});
    state.buffer.insert(state.buffer.end(), staging.begin(), staging.end());
    state.hasNewData = true;

    return true;
}

// ── Snapshot delta ────────────────────────────────────────────────────────

bool Chunk::buildSnapshotDelta(entt::registry& reg, uint32_t tickCount)
{
    // Snapshot delta supersedes all previous deltas (both tick deltas and prior snapshot deltas).
    // It contains ALL changes since the snapshot, so we can discard previous history.
    // Keep only the snapshot (first entry) and clear everything after it.
    if (!state.entries.empty()) {
        // Find the snapshot entry (always first, tick == snapshotTick)
        const auto& snapshotEntry = state.entries[0];
        
        // Truncate buffer to just the snapshot
        state.buffer.resize(snapshotEntry.length);
        
        // Remove all entries after the snapshot
        state.entries.resize(1);
    }
    
    return buildDeltaImpl(reg, tickCount,
        world.voxelsSnapshotDeltas,
        &DirtyComponent::snapshotDeltaType,
        &DirtyComponent::snapshotDirtyFlags,
        ServerMessageType::CHUNK_SNAPSHOT_DELTA,
        ServerMessageType::CHUNK_SNAPSHOT_DELTA_COMPRESSED);
}

// ── Tick delta ────────────────────────────────────────────────────────────

bool Chunk::buildTickDelta(entt::registry& reg, uint32_t tickCount)
{
    return buildDeltaImpl(reg, tickCount,
        world.voxelsTickDeltas,
        &DirtyComponent::tickDeltaType,
        &DirtyComponent::tickDirtyFlags,
        ServerMessageType::CHUNK_TICK_DELTA,
        ServerMessageType::CHUNK_TICK_DELTA_COMPRESSED);
}

// ── State update ───────────────────────────────────────────────────────────

bool Chunk::updateState(entt::registry& reg, uint32_t tickCount)
{
    // No serialization yet: build full snapshot
    if (state.isEmpty()) {
        buildSnapshot(reg, tickCount);
        return true;
    }

    // First call after snapshot, or every 20 calls: build snapshot delta
    const size_t deltaCount = state.getDeltaCount();
    if (deltaCount == 0 || deltaCount % 20 == 0) {
        return buildSnapshotDelta(reg, tickCount);
    }

    // Otherwise: build tick delta
    return buildTickDelta(reg, tickCount);
}

// ── Gateway data query ─────────────────────────────────────────────────────

void Chunk::getDataToSend(uint32_t lastReceivedTick, const uint8_t*& outData, size_t& outLength) const
{
    auto result = state.getDataToSend(lastReceivedTick);
    outData = result.first;
    outLength = result.second;
}

} // namespace voxelmmo
