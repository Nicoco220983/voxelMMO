#include "gateway/StateManager.hpp"
#include "common/MessageTypes.hpp"
#include <cstring>

namespace voxelmmo {

void StateManager::receiveChunkMessage(const uint8_t* data, size_t size) {
    if (size < 1 + sizeof(int64_t)) return;

    const auto msgType = static_cast<ChunkMessageType>(data[0]);
    ChunkId cid;
    std::memcpy(&cid.packed, data + 1, sizeof(int64_t));

    ChunkState& state = getChunkState(cid);

    switch (msgType) {
        case ChunkMessageType::SNAPSHOT_COMPRESSED:
            state.receiveSnapshot(data, size);
            break;
        case ChunkMessageType::SNAPSHOT_DELTA:
        case ChunkMessageType::SNAPSHOT_DELTA_COMPRESSED:
        case ChunkMessageType::TICK_DELTA:
        case ChunkMessageType::TICK_DELTA_COMPRESSED:
            state.receiveDelta(data, size);
            break;
        default:
            break;
    }
}

ChunkState& StateManager::getChunkState(ChunkId id) {
    return chunkStates[id];
}

const ChunkState* StateManager::findChunkState(ChunkId id) const {
    const auto it = chunkStates.find(id);
    return (it != chunkStates.end()) ? &it->second : nullptr;
}

void StateManager::removeChunk(ChunkId id) {
    chunkStates.erase(id);
}

void StateManager::setPlayerStateTick(PlayerId pid, ChunkId cid, uint32_t tick) {
    playerStateTick[pid][cid] = tick;
}

uint32_t StateManager::getPlayerStateTick(PlayerId pid, ChunkId cid) const {
    const auto pit = playerStateTick.find(pid);
    if (pit == playerStateTick.end()) return 0;
    const auto cit = pit->second.find(cid);
    return (cit != pit->second.end()) ? cit->second : 0;
}

void StateManager::removePlayer(PlayerId pid) {
    playerStateTick.erase(pid);
}

} // namespace voxelmmo
