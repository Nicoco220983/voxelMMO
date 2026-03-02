#include "game/Chunk.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
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

const std::vector<uint8_t>& Chunk::buildSnapshot(entt::registry& reg, uint32_t tickCount)
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

    BufWriter w{scratch.data(), entityOff};
    for (auto& [ent, ceid] : entities) {
        const auto& dyn   = reg.get<DynamicPositionComponent>(ent);
        const auto& etype = reg.get<EntityTypeComponent>(ent);
        w.write(ceid);
        w.write(static_cast<uint8_t>(etype.type));
        w.write<uint8_t>(POSITION_BIT);
        dyn.serializeFields(w);
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

    // Snapshot supersedes all accumulated deltas.
    // TODO: per-gateway delta tracking when multiple gateways are supported.
    state.snapshotTick = tickCount;
    state.deltas.clear();
    state.deltaOffsets.clear();
    state.hasNewDelta  = false;

    return buf;
}

// ── Snapshot delta ────────────────────────────────────────────────────────

bool Chunk::buildSnapshotDelta(entt::registry& reg, uint32_t tickCount)
{
    // Early exit: nothing to send
    if (world.voxelsSnapshotDeltas.empty()) {
        const bool anyDirty = std::any_of(entities.begin(), entities.end(),
            [&](const auto& kv) {
                return reg.get<DirtyComponent>(kv.first).isSnapshotDirty();
            });
        if (!anyDirty) return false;
    }

    // Build into a local staging buffer
    std::vector<uint8_t> staging;
    const size_t maxSize = HEADER_SIZE
        + sizeof(int32_t) + world.voxelsSnapshotDeltas.size() * (sizeof(uint16_t) + sizeof(uint8_t))
        + sizeof(int32_t) + entities.size() * 64;
    staging.resize(maxSize);

    size_t off = writeHeader(staging.data(), ChunkMessageType::SNAPSHOT_DELTA, tickCount);

    // Voxel delta section
    const int32_t voxelCount = static_cast<int32_t>(world.voxelsSnapshotDeltas.size());
    std::memcpy(staging.data() + off, &voxelCount, sizeof(int32_t));
    off += sizeof(int32_t);
    for (const auto& [vid, vtype] : world.voxelsSnapshotDeltas) {
        std::memcpy(staging.data() + off, &vid.packed, sizeof(uint16_t));
        off += sizeof(uint16_t);
        staging[off++] = vtype;
    }

    // Entity delta section
    const size_t entityCountOff = off;
    off += sizeof(int32_t);
    int32_t entityCount = 0;
    {
        BufWriter w{staging.data(), off};
        for (auto& [ent, ceid] : entities) {
            const uint8_t mask = reg.get<DirtyComponent>(ent).snapshotDirtyFlags;
            if (!mask) continue;
            w.write(static_cast<uint8_t>(DeltaType::UPDATE_ENTITY));
            w.write(ceid);
            w.write(static_cast<uint8_t>(reg.get<EntityTypeComponent>(ent).type));
            w.write(mask);
            if (mask & POSITION_BIT)
                reg.get<DynamicPositionComponent>(ent).serializeFields(w);
            ++entityCount;
        }
    }
    std::memcpy(staging.data() + entityCountOff, &entityCount, sizeof(int32_t));
    staging.resize(off);

    maybeCompressDelta(staging, ChunkMessageType::SNAPSHOT_DELTA_COMPRESSED);

    // Append to the unified delta buffer
    state.deltaOffsets.push_back({tickCount, state.deltas.size()});
    state.deltas.insert(state.deltas.end(), staging.begin(), staging.end());
    state.hasNewDelta = true;
    return true;
}

// ── Tick delta ────────────────────────────────────────────────────────────

bool Chunk::buildTickDelta(entt::registry& reg, uint32_t tickCount)
{
    // Early exit: nothing to send
    if (world.voxelsTickDeltas.empty()) {
        const bool anyDirty = std::any_of(entities.begin(), entities.end(),
            [&](const auto& kv) {
                return reg.get<DirtyComponent>(kv.first).isTickDirty();
            });
        if (!anyDirty) return false;
    }

    // Build into a local staging buffer
    std::vector<uint8_t> staging;
    const size_t maxSize = HEADER_SIZE
        + sizeof(int32_t) + world.voxelsTickDeltas.size() * (sizeof(uint16_t) + sizeof(uint8_t))
        + sizeof(int32_t) + entities.size() * 64;
    staging.resize(maxSize);

    size_t off = writeHeader(staging.data(), ChunkMessageType::TICK_DELTA, tickCount);

    // Voxel delta section
    const int32_t voxelCount = static_cast<int32_t>(world.voxelsTickDeltas.size());
    std::memcpy(staging.data() + off, &voxelCount, sizeof(int32_t));
    off += sizeof(int32_t);
    for (const auto& [vid, vtype] : world.voxelsTickDeltas) {
        std::memcpy(staging.data() + off, &vid.packed, sizeof(uint16_t));
        off += sizeof(uint16_t);
        staging[off++] = vtype;
    }

    // Entity delta section
    const size_t entityCountOff = off;
    off += sizeof(int32_t);
    int32_t entityCount = 0;
    {
        BufWriter w{staging.data(), off};
        for (auto& [ent, ceid] : entities) {
            const uint8_t mask = reg.get<DirtyComponent>(ent).tickDirtyFlags;
            if (!mask) continue;
            w.write(static_cast<uint8_t>(DeltaType::UPDATE_ENTITY));
            w.write(ceid);
            w.write(static_cast<uint8_t>(reg.get<EntityTypeComponent>(ent).type));
            w.write(mask);
            if (mask & POSITION_BIT)
                reg.get<DynamicPositionComponent>(ent).serializeFields(w);
            ++entityCount;
        }
    }
    std::memcpy(staging.data() + entityCountOff, &entityCount, sizeof(int32_t));
    staging.resize(off);

    maybeCompressDelta(staging, ChunkMessageType::TICK_DELTA_COMPRESSED);

    // Append to the unified delta buffer
    state.deltaOffsets.push_back({tickCount, state.deltas.size()});
    state.deltas.insert(state.deltas.end(), staging.begin(), staging.end());
    state.hasNewDelta = true;
    return true;
}

} // namespace voxelmmo
