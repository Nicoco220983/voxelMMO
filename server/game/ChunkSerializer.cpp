#include "game/ChunkSerializer.hpp"
#include "game/Chunk.hpp"
#include "game/ChunkRegistry.hpp"
#include "game/components/DirtyComponent.hpp"
#include <entt/entt.hpp>

namespace voxelmmo {

ChunkSerializer::ChunkSerializer() {
    // Pre-allocate scratch to avoid repeated allocations
    scratch.reserve(64 * 1024);
}

void ChunkSerializer::clear() {
    chunkBuf.clear();
    // scratch kept for reuse
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

size_t ChunkSerializer::serializeAllChunks(entt::registry& registry, ChunkRegistry& chunkRegistry, uint32_t tick) {
    ensureScratch(64 * 1024);
    size_t chunksSerialized = 0;
    
    // Serialize all chunks into the concatenated buffer
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        auto& serState = chunkStates_[cid];
        
        // Determine what type of message to build based on serialization state
        size_t bytesWritten = 0;
        bool isSnapshot = false;
        bool isSnapshotDelta = false;
        
        if (!serState.hasBeenSerialized) {
            // First time serializing this chunk: build full snapshot
            bytesWritten = chunkPtr->buildSnapshot(registry, tick, chunkBuf, scratch);
            serState.hasBeenSerialized = true;
            serState.lastSnapshotTick = tick;
            serState.deltaCount = 0;
            isSnapshot = true;
        } else {
            // Check if we should build snapshot delta (every 20 ticks or first delta after snapshot)
            if (serState.deltaCount == 0 || serState.deltaCount % 20 == 0) {
                bytesWritten = chunkPtr->buildSnapshotDelta(registry, tick, chunkBuf, scratch);
                if (bytesWritten > 0) {
                    serState.lastSnapshotTick = tick;
                    serState.deltaCount = 0;
                    isSnapshotDelta = true;
                }
            } else {
                // Build tick delta
                bytesWritten = chunkPtr->buildTickDelta(registry, tick, chunkBuf, scratch);
            }
        }
        
        if (bytesWritten > 0) {
            serState.deltaCount++;
            chunksSerialized++;
            
            // Clear dirty flags based on what was serialized
            if (isSnapshot || isSnapshotDelta) {
                // Snapshot or snapshot delta: clear all dirty flags
                chunkPtr->clearEntityDirtyFlags(registry, /*clearSnapshotFlags=*/true);
            } else {
                // Tick delta: clear only tick flags
                chunkPtr->clearEntityDirtyFlags(registry, /*clearSnapshotFlags=*/false);
            }
        }
    }
    
    return chunksSerialized;
}

} // namespace voxelmmo
