#include "gateway/GatewayEngine.hpp"
#include "common/MessageTypes.hpp"
#include "common/NetworkProtocol.hpp"
#include <iostream>
#include <cstring>
#include <vector>

namespace voxelmmo {

GatewayEngine::GatewayEngine()  = default;
GatewayEngine::~GatewayEngine() = default;

void GatewayEngine::setPlayerConnectCallback(PlayerConnectCallback cb)       { connectCb    = std::move(cb); }
void GatewayEngine::setPlayerDisconnectCallback(PlayerDisconnectCallback cb)  { disconnectCb = std::move(cb); }
void GatewayEngine::setPlayerInputCallback(PlayerInputCallback cb)             { inputCb      = std::move(cb); }

// ── Incoming from game engine ─────────────────────────────────────────────

void GatewayEngine::receiveGameMessage(const uint8_t* data, size_t size) {
    // Copy the batch so it can be safely captured across the thread boundary
    auto buf = std::make_shared<std::vector<uint8_t>>(data, data + size);

    uwsLoop->defer([this, buf]() {
        // Parse individual messages using their embedded [type(1)][size(2)] header
        // and update chunk state for each message.
        size_t off = 0;
        while (off + 3 <= buf->size()) {
            // Read size from header (bytes 1-2, little-endian)
            uint16_t msgSize;
            std::memcpy(&msgSize, buf->data() + off + 1, 2);
            if (off + msgSize > buf->size()) break;
            
            // Extract chunkId and tick from the 15-byte header
            // [type(1)][size(2)][chunk_id(8)][tick(4)]
            ChunkId cid;
            std::memcpy(&cid.packed, buf->data() + off + 3, sizeof(int64_t));
            uint32_t tick;
            std::memcpy(&tick, buf->data() + off + 11, sizeof(uint32_t));
            
            // Store in chunk state cache
            receiveChunkMessage(buf->data() + off, msgSize);
            
            // Filter: send only to players watching this chunk
            processChunkUpdate(cid, buf->data() + off, msgSize, tick);
            
            off += msgSize;
        }
    });
}

void GatewayEngine::receiveGameMessageForPlayer(PlayerId playerId, const uint8_t* data, size_t size) {
    // Copy the message so it can be safely captured across the thread boundary
    auto buf = std::make_shared<std::vector<uint8_t>>(data, data + size);

    uwsLoop->defer([this, playerId, buf]() {
        sendToPlayer(playerId, buf->data(), buf->size());
    });
}

void GatewayEngine::broadcastBatch(const uint8_t* data, size_t size) {
    const std::string_view msg(reinterpret_cast<const char*>(data), size);
    for (auto& [pid, ws] : sockets) {
        ws->send(msg, uWS::OpCode::BINARY);
    }
}

bool GatewayEngine::sendToPlayer(PlayerId playerId, const uint8_t* data, size_t size) {
    auto it = sockets.find(playerId);
    if (it == sockets.end()) return false;
    const std::string_view msg(reinterpret_cast<const char*>(data), size);
    it->second->send(msg, uWS::OpCode::BINARY);
    return true;
}

// ── WebSocket server ──────────────────────────────────────────────────────

void GatewayEngine::listen(int port) {
    // Capture the loop pointer now, while we are on the uWS thread.
    // receiveGameMessage() will use this from the game thread.
    uwsLoop = uWS::Loop::get();

    wsApp.ws<PlayerConnection>("/*", {
        /* Settings */
        .maxPayloadLength = 16 * 1024,
        .idleTimeout      = 120,

        .open = [this](uWS::WebSocket<false, true, PlayerConnection>* ws) {
            const PlayerId pid = nextPlayerId++;
            ws->getUserData()->playerId = pid;
            sockets[pid] = ws;
            std::cout << "[gateway] Player " << pid << " connected\n";
            if (connectCb) connectCb(pid);
        },

        .message = [this](uWS::WebSocket<false, true, PlayerConnection>* ws,
                          std::string_view msg, uWS::OpCode opCode) {
            if (opCode != uWS::OpCode::BINARY) return;
            const PlayerId pid = ws->getUserData()->playerId;
            if (inputCb) {
                inputCb(pid,
                        reinterpret_cast<const uint8_t*>(msg.data()),
                        msg.size());
            }
        },

        .close = [this](uWS::WebSocket<false, true, PlayerConnection>* ws,
                        int /*code*/, std::string_view /*reason*/) {
            const PlayerId pid = ws->getUserData()->playerId;
            sockets.erase(pid);
            removePlayer(pid);
            std::cout << "[gateway] Player " << pid << " disconnected\n";
            if (disconnectCb) disconnectCb(pid);
        },
    })
    .listen(port, [port](us_listen_socket_t* listenSocket) {
        if (listenSocket) {
            std::cout << "[gateway] Listening on port " << port << "\n";
        } else {
            std::cerr << "[gateway] Failed to bind port " << port << "\n";
        }
    })
    .run();
}

// ── Per-player chunk watching ─────────────────────────────────────────────

void GatewayEngine::setPlayerWatchedChunks(PlayerId pid, const std::set<ChunkId>& chunks) {
    players[pid].watchedChunks = chunks;
}

void GatewayEngine::sendInitialChunksToPlayer(PlayerId pid, uint32_t currentTick) {
    auto it = players.find(pid);
    if (it == players.end()) return;
    
    PlayerInfo& pinfo = it->second;
    
    // Build batch: for each watched chunk, send full snapshot (lastTick=0)
    std::vector<uint8_t> batch;
    for (ChunkId cid : pinfo.watchedChunks) {
        const ChunkState* state = findChunkState(cid);
        if (!state) continue;  // Chunk not yet in cache (will come in future tick)
        
        auto [dataPtr, dataLen] = state->getDataToSend(0);  // Full buffer
        if (dataLen > 0) {
            batch.insert(batch.end(), dataPtr, dataPtr + dataLen);
            pinfo.lastStateTick[cid] = currentTick;
        }
    }
    
    if (!batch.empty()) {
        sendToPlayer(pid, batch.data(), batch.size());
    }
}

void GatewayEngine::processChunkUpdate(ChunkId cid, const uint8_t* data, size_t size, uint32_t tick) {
    (void)data;  // data is already stored in ChunkState via receiveChunkMessage
    (void)size;
    
    const ChunkState* state = findChunkState(cid);
    if (!state) return;
    
    // For each player watching this chunk, send appropriate data slice
    for (auto& [pid, pinfo] : players) {
        if (pinfo.watchedChunks.find(cid) == pinfo.watchedChunks.end()) {
            continue;  // Player not watching this chunk
        }
        
        const uint32_t lastTick = pinfo.lastStateTick[cid];  // 0 if new
        
        // Use ChunkState::getDataToSend to determine what this player needs
        auto [dataPtr, dataLen] = state->getDataToSend(lastTick);
        
        if (dataLen > 0) {
            sendToPlayer(pid, dataPtr, dataLen);
            pinfo.lastStateTick[cid] = tick;
        }
    }
}

void GatewayEngine::removePlayer(PlayerId pid) {
    players.erase(pid);
}

// ── Per-chunk state management ────────────────────────────────────────────

void GatewayEngine::receiveChunkMessage(const uint8_t* data, size_t size) {
    // Minimum header: [type(1)][size(2)][chunk_id(8)][tick(4)] = 15 bytes
    if (size < NetworkProtocol::CHUNK_MESSAGE_HEADER_SIZE) return;

    // size is at bytes 1-2, chunk_id at bytes 3-10
    ChunkId cid;
    std::memcpy(&cid.packed, data + 3, sizeof(int64_t));

    ChunkState& state = getChunkState(cid);
    
    // receiveMessage handles all message types appropriately:
    // - Snapshots: clear all existing data
    // - Snapshot deltas: clear previous deltas (keep snapshot), append delta
    // - Tick deltas: just append
    state.receiveMessage(data, size);
}

ChunkState& GatewayEngine::getChunkState(ChunkId id) {
    return chunkStates[id];
}

const ChunkState* GatewayEngine::findChunkState(ChunkId id) const {
    const auto it = chunkStates.find(id);
    return (it != chunkStates.end()) ? &it->second : nullptr;
}

void GatewayEngine::removeChunk(ChunkId id) {
    chunkStates.erase(id);
}



} // namespace voxelmmo
