#include "game/Chunk.hpp"
#include <lz4.h>
#include <algorithm>
#include <cstring>

namespace voxelmmo {

Chunk::Chunk(ChunkId chunkId) : id(chunkId) {
    // Reserve scratch once; it grows on demand but rarely needs to.
    state.scratch.reserve(64 * 1024);
}

size_t Chunk::writeHeader(uint8_t* buf, ChunkMessageType msgType, uint32_t tickCount) const {
    buf[0] = static_cast<uint8_t>(msgType);
    std::memcpy(buf + 1, &id.packed, sizeof(int64_t));
    std::memcpy(buf + 9, &tickCount, sizeof(uint32_t));
    return HEADER_SIZE;
}

// ── Delta compression helper ──────────────────────────────────────────────

void Chunk::maybeCompressDelta(std::vector<uint8_t>& buf, ChunkMessageType compressedType) {
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

const std::vector<uint8_t>& Chunk::buildSnapshot(
    entt::registry& reg,
    const std::unordered_map<EntityId, std::unique_ptr<BaseEntity>>& entityMap,
    uint32_t tickCount)
{
    auto& buf = state.snapshot;
    buf.clear();

    // ── Header (13 bytes) + flags byte ───────────────────────────────────
    buf.resize(HEADER_SIZE + 1);  // [0..12] header, [13] flags
    writeHeader(buf.data(), ChunkMessageType::SNAPSHOT_COMPRESSED, tickCount);
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
    // Upper bound: int32 count + 64 bytes per entity (actual max ~28 bytes)
    scratch.resize(sizeof(int32_t) + entities.size() * 64);
    size_t entityOff = sizeof(int32_t);  // leave room for count prefix
    int32_t entityCount = 0;

    for (EntityId eid : entities) {
        auto it = entityMap.find(eid);
        if (it == entityMap.end()) continue;
        it->second->serializeSnapshot(scratch.data(), entityOff);
        ++entityCount;
    }
    scratch.resize(entityOff);
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
        const int32_t storedSize = static_cast<int32_t>(scratch.size());
        std::memcpy(buf.data() + entitySizeOff, &storedSize, sizeof(int32_t));
        buf.insert(buf.end(), scratch.begin(), scratch.end());
    }

    buf[HEADER_SIZE] = flags;
    return buf;
}

// ── Snapshot delta ────────────────────────────────────────────────────────

const std::vector<uint8_t>& Chunk::buildSnapshotDelta(
    entt::registry& reg,
    const std::unordered_map<EntityId, std::unique_ptr<BaseEntity>>& entityMap,
    uint32_t tickCount)
{
    auto& buf = state.snapshotDelta;

    // Early exit: nothing to send
    if (world.voxelsSnapshotDeltas.empty()) {
        const bool anyDirty = std::any_of(entities.begin(), entities.end(),
            [&](EntityId eid) {
                auto it = entityMap.find(eid);
                return it != entityMap.end() && it->second->isSnapshotDirty();
            });
        if (!anyDirty) { buf.clear(); return buf; }
    }

    // Pre-allocate to the safe upper bound, then trim at the end
    const size_t maxSize = HEADER_SIZE
        + sizeof(int32_t) + world.voxelsSnapshotDeltas.size() * (sizeof(uint16_t) + sizeof(uint8_t))
        + sizeof(int32_t) + entities.size() * 64;
    buf.resize(maxSize);

    size_t off = writeHeader(buf.data(), ChunkMessageType::SNAPSHOT_DELTA, tickCount);

    // Voxel delta section
    const int32_t voxelCount = static_cast<int32_t>(world.voxelsSnapshotDeltas.size());
    std::memcpy(buf.data() + off, &voxelCount, sizeof(int32_t));
    off += sizeof(int32_t);
    for (const auto& [vid, vtype] : world.voxelsSnapshotDeltas) {
        std::memcpy(buf.data() + off, &vid.packed, sizeof(uint16_t));
        off += sizeof(uint16_t);
        buf[off++] = vtype;
    }

    // Entity delta section
    const size_t entityCountOff = off;
    off += sizeof(int32_t);
    int32_t entityCount = 0;
    for (EntityId eid : entities) {
        auto it = entityMap.find(eid);
        if (it == entityMap.end()) continue;
        const BaseEntity& entity = *it->second;
        if (!entity.isSnapshotDirty()) continue;
        const uint8_t mask = reg.get<DirtyComponent>(entity.handle).snapshotDirtyFlags;
        buf[off++] = static_cast<uint8_t>(DeltaType::UPDATE_ENTITY);
        entity.serializeDelta(buf.data(), off, mask);
        ++entityCount;
    }
    std::memcpy(buf.data() + entityCountOff, &entityCount, sizeof(int32_t));
    buf.resize(off);

    maybeCompressDelta(buf, ChunkMessageType::SNAPSHOT_DELTA_COMPRESSED);
    return buf;
}

// ── Tick delta ────────────────────────────────────────────────────────────

const std::vector<uint8_t>& Chunk::buildTickDelta(
    entt::registry& reg,
    const std::unordered_map<EntityId, std::unique_ptr<BaseEntity>>& entityMap,
    uint32_t tickCount)
{
    // Early exit: nothing to send
    if (world.voxelsTickDeltas.empty()) {
        const bool anyDirty = std::any_of(entities.begin(), entities.end(),
            [&](EntityId eid) {
                auto it = entityMap.find(eid);
                return it != entityMap.end() && it->second->isTickDirty();
            });
        if (!anyDirty) {
            // Return a stable empty reference without touching tickDeltas
            static const std::vector<uint8_t> empty{};
            return empty;
        }
    }

    state.tickDeltas.emplace_back();
    auto& buf = state.tickDeltas.back();

    const size_t maxSize = HEADER_SIZE
        + sizeof(int32_t) + world.voxelsTickDeltas.size() * (sizeof(uint16_t) + sizeof(uint8_t))
        + sizeof(int32_t) + entities.size() * 64;
    buf.resize(maxSize);

    size_t off = writeHeader(buf.data(), ChunkMessageType::TICK_DELTA, tickCount);

    // Voxel delta section
    const int32_t voxelCount = static_cast<int32_t>(world.voxelsTickDeltas.size());
    std::memcpy(buf.data() + off, &voxelCount, sizeof(int32_t));
    off += sizeof(int32_t);
    for (const auto& [vid, vtype] : world.voxelsTickDeltas) {
        std::memcpy(buf.data() + off, &vid.packed, sizeof(uint16_t));
        off += sizeof(uint16_t);
        buf[off++] = vtype;
    }

    // Entity delta section
    const size_t entityCountOff = off;
    off += sizeof(int32_t);
    int32_t entityCount = 0;
    for (EntityId eid : entities) {
        auto it = entityMap.find(eid);
        if (it == entityMap.end()) continue;
        const BaseEntity& entity = *it->second;
        if (!entity.isTickDirty()) continue;
        const uint8_t mask = reg.get<DirtyComponent>(entity.handle).tickDirtyFlags;
        buf[off++] = static_cast<uint8_t>(DeltaType::UPDATE_ENTITY);
        entity.serializeDelta(buf.data(), off, mask);
        ++entityCount;
    }
    std::memcpy(buf.data() + entityCountOff, &entityCount, sizeof(int32_t));
    buf.resize(off);

    maybeCompressDelta(buf, ChunkMessageType::TICK_DELTA_COMPRESSED);
    return buf;
}

} // namespace voxelmmo
