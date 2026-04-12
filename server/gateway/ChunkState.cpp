#include "gateway/ChunkState.hpp"
#include <cstring>

namespace voxelmmo {

std::pair<const uint8_t*, size_t> ChunkState::getDataToSend(uint32_t lastReceivedTick) const {
    if (entries.empty()) {
        return {nullptr, 0};
    }

    // New watcher (lastReceivedTick == 0) or no matching entry: send everything
    if (lastReceivedTick == 0) {
        return {buffer.data(), buffer.size()};
    }

    // Find first entry with tick > lastReceivedTick
    for (const auto& entry : entries) {
        if (entry.tick > lastReceivedTick) {
            return {buffer.data() + entry.offset, buffer.size() - entry.offset};
        }
    }

    // Recipient is up to date (all entries have tick <= lastReceivedTick)
    return {nullptr, 0};
}

uint32_t ChunkState::getLatestTick() const {
    if (entries.empty()) return 0;
    return entries.back().tick;
}

bool ChunkState::isEmpty() const {
    return buffer.empty();
}

bool ChunkState::hasOnlySnapshot() const {
    return entries.size() == 1;
}

size_t ChunkState::getDeltaCount() const {
    return entries.empty() ? 0 : entries.size() - 1;
}

void ChunkState::clear() {
    buffer.clear();
    entries.clear();
    hasNewData = false;
}

void ChunkState::receiveMessage(const uint8_t* data, size_t size) {
    if (size < 1) return;
    
    const uint8_t msgType = data[0];
    
    // Extract tick from header (bytes 11-14)
    uint32_t tick = 0;
    if (size >= 15) std::memcpy(&tick, data + 11, sizeof(uint32_t));
    
    // Check message type to determine handling
    const bool isSnapshot = (msgType == 0 || msgType == 1);  // CHUNK_SNAPSHOT[_COMPRESSED]
    const bool isSnapshotDelta = (msgType == 2 || msgType == 3);  // CHUNK_SNAPSHOT_DELTA[_COMPRESSED]
    
    if (isSnapshot) {
        // Full snapshot: clear everything and start fresh
        clear();
        buffer.assign(data, data + size);
        entries.push_back({tick, 0, size});
    } else if (isSnapshotDelta) {
        // Snapshot delta: supersedes all previous deltas, keep only snapshot
        if (!entries.empty()) {
            // Keep only the first entry (snapshot)
            const auto& snapshotEntry = entries[0];
            buffer.resize(snapshotEntry.length);
            entries.resize(1);
        }
        // Append the snapshot delta
        size_t offset = buffer.size();
        entries.push_back({tick, offset, size});
        buffer.insert(buffer.end(), data, data + size);
    } else {
        // Tick delta: just append
        size_t offset = buffer.size();
        entries.push_back({tick, offset, size});
        buffer.insert(buffer.end(), data, data + size);
    }
    
    hasNewData = true;
}

} // namespace voxelmmo
