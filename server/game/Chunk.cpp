#include "game/Chunk.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/SheepBehaviorComponent.hpp"
#include "game/components/PendingDeleteComponent.hpp"
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
    auto& buf = state.snapshot;
    buf.clear();

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
    // Upper bound: int32 count + 64 bytes per entity (actual max ~28 bytes)
    scratch.resize(sizeof(int32_t) + entities.size() * 64);
    size_t entityOff = sizeof(int32_t);  // leave room for count prefix
    int32_t entityCount = 0;

    BufWriter w{scratch.data(), entityOff};
    for (auto ent : entities) {
        const auto& dyn   = reg.get<DynamicPositionComponent>(ent);
        const auto& etype = reg.get<EntityTypeComponent>(ent);
        const auto& gid   = reg.get<GlobalEntityIdComponent>(ent);
        w.write(gid.id);                          // uint32 GlobalEntityId (was uint16 ChunkEntityId)
        w.write(static_cast<uint8_t>(etype.type));
        
        // Build component flags
        uint8_t flags = POSITION_BIT;
        if (etype.type == EntityType::SHEEP) {
            flags |= SHEEP_BEHAVIOR_BIT;
        }
        w.write(flags);
        
        dyn.serializeFields(w);
        
        // Serialize sheep behavior if present
        if (etype.type == EntityType::SHEEP) {
            const auto& behavior = reg.get<SheepBehaviorComponent>(ent);
            behavior.serializeFields(w);
        }
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

    // Snapshot supersedes all accumulated deltas.
    // TODO: per-gateway delta tracking when multiple gateways are supported.
    state.snapshotTick = tickCount;
    state.deltas.clear();
    state.deltaOffsets.clear();
    state.hasNewDelta  = false;

    return buf;
}

// ── Delta (shared implementation) ─────────────────────────────────────────

bool Chunk::buildDeltaImpl(
    entt::registry& reg,
    uint32_t tickCount,
    const std::vector<std::pair<VoxelIndex, VoxelType>>& voxelDeltas,
    uint8_t DirtyComponent::* flagsField,
    ServerMessageType rawType,
    ServerMessageType compressedType)
{
    // Early exit: nothing to send
    if (voxelDeltas.empty()) {
        const bool anyDirty = std::any_of(entities.begin(), entities.end(),
            [&](entt::entity ent) {
                return reg.get<DirtyComponent>(ent).*flagsField != 0;
            });
        if (!anyDirty) return false;
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
    int32_t entityCount = 0;
    {
        BufWriter w{staging.data(), off};
        for (auto ent : entities) {
            auto& dirty = reg.get<DirtyComponent>(ent);
            const uint8_t mask = dirty.*flagsField;
            if (!mask) continue;
            const auto& gid = reg.get<GlobalEntityIdComponent>(ent);
            const auto etype = reg.get<EntityTypeComponent>(ent).type;
            
            // Determine delta type based on lifecycle flags and movedEntities
            DeltaType deltaType;
            if (reg.all_of<PendingDeleteComponent>(ent)) {
                deltaType = DeltaType::DELETE_ENTITY;
            } else if (leftEntities.count(ent)) {
                // Entity is leaving this chunk - old chunk sends CHUNK_CHANGE
                deltaType = DeltaType::CHUNK_CHANGE_ENTITY;
            } else if (mask & DirtyComponent::CREATED_BIT) {
                deltaType = DeltaType::CREATE_ENTITY;
            } else {
                deltaType = DeltaType::UPDATE_ENTITY;
            }
            
            w.write(static_cast<uint8_t>(deltaType));
            w.write(gid.id);  // uint32 GlobalEntityId
            
            if (deltaType == DeltaType::DELETE_ENTITY) {
                // DELETE: just GlobalEntityId, no additional data
                ++entityCount;
                continue;
            }
            
            if (deltaType == DeltaType::CHUNK_CHANGE_ENTITY) {
                // CHUNK_CHANGE: include new chunk ID computed from position
                const auto& dyn = reg.get<DynamicPositionComponent>(ent);
                const ChunkId newChunkId = ChunkId::make(
                    dyn.y >> CHUNK_SHIFT_Y,
                    dyn.x >> CHUNK_SHIFT_X,
                    dyn.z >> CHUNK_SHIFT_Z
                );
                w.write(newChunkId.packed);
                ++entityCount;
                continue;
            }
            
            // CREATE and UPDATE: include EntityType and component data
            w.write(static_cast<uint8_t>(etype));
            
            // Component mask (strip lifecycle bits, keep only component bits 0-5)
            const uint8_t componentMask = mask & 0x3F;
            w.write(componentMask);
            
            if (componentMask & POSITION_BIT)
                reg.get<DynamicPositionComponent>(ent).serializeFields(w);
            if (componentMask & SHEEP_BEHAVIOR_BIT)
                reg.get<SheepBehaviorComponent>(ent).serializeFields(w);
            ++entityCount;

            // Note: Dirty flags are cleared centrally in GameEngine::clearAllDirtyFlags()
            // after all serialization is complete for the tick.
        }
    }
    std::memcpy(staging.data() + entityCountOff, &entityCount, sizeof(int32_t));
    staging.resize(off);

    maybeCompressDelta(staging, compressedType);

    // Fill in the size field (bytes 1-2) now that staging is complete
    const uint16_t msgSize = static_cast<uint16_t>(staging.size());
    staging[1] = static_cast<uint8_t>(msgSize & 0xFF);
    staging[2] = static_cast<uint8_t>((msgSize >> 8) & 0xFF);

    // Append to the unified delta buffer
    state.deltaOffsets.push_back({tickCount, state.deltas.size()});
    state.deltas.insert(state.deltas.end(), staging.begin(), staging.end());
    state.hasNewDelta = true;

    return true;
}

// ── Snapshot delta ────────────────────────────────────────────────────────

bool Chunk::buildSnapshotDelta(entt::registry& reg, uint32_t tickCount)
{
    return buildDeltaImpl(reg, tickCount,
        world.voxelsSnapshotDeltas,
        &DirtyComponent::snapshotDirtyFlags,
        ServerMessageType::CHUNK_SNAPSHOT_DELTA,
        ServerMessageType::CHUNK_SNAPSHOT_DELTA_COMPRESSED);
}

// ── Tick delta ────────────────────────────────────────────────────────────

bool Chunk::buildTickDelta(entt::registry& reg, uint32_t tickCount)
{
    return buildDeltaImpl(reg, tickCount,
        world.voxelsTickDeltas,
        &DirtyComponent::tickDirtyFlags,
        ServerMessageType::CHUNK_TICK_DELTA,
        ServerMessageType::CHUNK_TICK_DELTA_COMPRESSED);
}

// ── State update ───────────────────────────────────────────────────────────

bool Chunk::updateState(entt::registry& reg, uint32_t tickCount)
{
    // No snapshot yet: build full snapshot
    if (state.snapshot.empty()) {
        deltaCallCount = 0;
        buildSnapshot(reg, tickCount);
        return true;
    }

    // First call after snapshot, or every 20 calls: build snapshot delta
    if (deltaCallCount == 0 || deltaCallCount % 20 == 0) {
        ++deltaCallCount;
        return buildSnapshotDelta(reg, tickCount);
    }

    // Otherwise: build tick delta
    ++deltaCallCount;
    return buildTickDelta(reg, tickCount);
}

} // namespace voxelmmo
